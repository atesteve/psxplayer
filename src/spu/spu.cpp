// SPDX-FileCopyrightText: 2026 Aitor Esteve Alvarado
// SPDX-License-Identifier: GPL-3.0-only

#include "spu.h"
#include "spu_registers.h"
#include "adpcm.h"
#include "core/dma.h"
#include "gauss.h"

#include <fmt/format.h>

#include <cstddef>
#include <utility>
#include <numeric>

namespace {

// CPU cycles per each sample emmitted by the SPU, including both channels (L and R).
constexpr uint64_t SAMPLE_RATE_CYCLES = 768;

constexpr size_t spu_offset(r3000_ptr_t addr, ...)
{
    return addr - SPU_BASE;
}

constexpr bool in_range(r3000_ptr_t addr, r3000_ptr_t range)
{
    return addr == range;
}

constexpr bool in_range(r3000_ptr_t addr, r3000_ptr_t start, r3000_ptr_t end)
{
    return addr >= start && addr < end;
}

constexpr size_t range_size(r3000_ptr_t)
{
    return 2;
}

constexpr size_t range_size(r3000_ptr_t start, r3000_ptr_t end)
{
    return end - start;
}

constexpr auto get_first(auto first, ...)
{
    return first;
}

uint32_t write_32bit_reg(uint32_t* reg, uint16_t value, r3000_ptr_t offset)
{
    if (offset == 0) {
        uint32_t const effective_value = value;
        *reg = (*reg & 0xffff0000) | effective_value;
        return effective_value;
    } else {
        uint32_t const effective_value = uint32_t(value) << 16;
        *reg = (*reg & 0x0000ffff) | effective_value;
        return effective_value;
    }
}

uint16_t read_32bit_reg(uint32_t const& reg, r3000_ptr_t offset)
{
    if (offset == 0) {
        return reg;
    } else {
        return reg >> 16;
    }
}

constexpr uint64_t get_sample_cycle(uint64_t clock_cycle)
{
    return clock_cycle / SAMPLE_RATE_CYCLES;
}

constexpr int16_t smooth_clamp(int32_t sample)
{
    // This is essentially fixed-point math, that's why it looks kind of funky.
    // Assuming the range -32768..32767 maps to -1..1, this code implements this function:
    // sample = clamp(sample, -1.5, 1.5);
    // sample = sample / 3 + 0.5;
    // sample = sample * sample * (6 - 4 * sample) - 1;

    int64_t sample64 = std::clamp(sample, -49152, 49152); // 1.5 times the range size.
    sample64 = sample64 / 3 + 16385;
    auto const sample_sq = (sample64 * sample64) >> 15;
    auto const third_term = (196608 - 4 * sample64);
    sample64 = (sample_sq * third_term) >> 15;
    sample64 -= 32768;
    return sample64;
}

} // namespace

struct SampleBuffer {
    uint16_t pos{};
    uint16_t frac{};
    std::array<int16_t, 28> samples{};

    void reset()
    {
        pos = samples.size();
        frac = 0;
        std::ranges::fill(samples, 0);
    }

    bool empty() { return pos >= samples.size(); }

    void loaded_block() { pos -= samples.size(); }

    auto last_samples() const
    {
        return std::make_pair(samples[samples.size() - 1], samples[samples.size() - 2]);
    }

    int16_t sample_and_advance() { return samples[pos++]; }

    uint8_t get_fractional_index() const { return frac >> 4; }
};

struct FilterBuffer {
    uint64_t buf{};

    void reset() { buf = 0; }

    // Intentionally unsigned integer to avoid sign extension in the bit operation.
    void push(uint16_t sample)
    {
        buf <<= 16;
        buf |= sample;
    }

    int16_t get_filtered(uint8_t fract) const
    {
        auto const samples = std::bit_cast<std::array<int16_t, 4>>(buf);
        auto const gauss_entry = psx_gauss_table[fract];
        auto const gauss_entries = std::bit_cast<std::array<int16_t, 4>>(gauss_entry);

        int32_t out = 0;
        for (auto i = 0u; i < samples.size(); i++) {
            int32_t const sample = samples[i];
            out += gauss_entries[i] * sample;
        }
        return out >> 15;
    }
};

enum class AdsrPhase : uint8_t {
    ATTACK,
    DECAY,
    SUSTAIN,
    RELEASE,
};

struct AdsrState {
    uint16_t counter{};
    AdsrPhase phase{};

    void start() { *this = {}; }

    void release() { phase = AdsrPhase::RELEASE; }

    bool tick(voice_registers_t& regs)
    {
        using enum AdsrPhase;

        struct Settings {
            bool exponential;
            bool decrease;
            int shift;
            int step;
        };

        auto const settings = [&] -> Settings {
            switch (phase) {
            case ATTACK:
                return {
                    .exponential = regs.adsr.fields.attack_exp,
                    .decrease = false,
                    .shift = regs.adsr.fields.attack_shift,
                    .step = regs.adsr.fields.attack_step,
                };
            case DECAY:
                return {
                    .exponential = true,
                    .decrease = true,
                    .shift = regs.adsr.fields.decay_shift,
                    .step = 0,
                };
            case SUSTAIN:
                return {
                    .exponential = regs.adsr.fields.sustain_exp,
                    .decrease = regs.adsr.fields.sustain_dir,
                    .shift = regs.adsr.fields.sustain_shift,
                    .step = regs.adsr.fields.sustain_step,
                };
            case RELEASE:
                return {
                    .exponential = regs.adsr.fields.release_exp,
                    .decrease = true,
                    .shift = regs.adsr.fields.release_shift,
                    .step = 0,
                };
            default:
                std::unreachable();
            }
        }();

        auto& volume = regs.adsr_vol;

        int vol_inc = 7 - settings.step;
        if (settings.decrease) {
            // Bitwise negation, not arithmetic negation.
            vol_inc = ~vol_inc;
        }
        int vol_shift = std::max(0, 11 - settings.shift);
        int counter_shift = std::max(0, settings.shift - 11);
        vol_inc <<= vol_shift;
        int counter_inc = 0x8000 >> counter_shift;
        if (settings.exponential && !settings.decrease && volume > 0x6000) {
            vol_inc >>= 1;
            counter_inc >>= 1;
            if (settings.shift < 10) {
                vol_inc >>= 1;
            } else if (settings.shift >= 11) {
                counter_inc >>= 1;
            }
        } else if (settings.exponential && settings.decrease) {
            vol_inc = (vol_inc * volume) >> 15;
        }

        counter += counter_inc;
        if (counter < 0x8000) {
            return false;
        }

        counter = 0;
        volume = std::saturating_add<int16_t>(volume, vol_inc);

        switch (phase) {
        case ATTACK:
            if (volume == std::numeric_limits<int16_t>::max()) {
                phase = DECAY;
            }
            return false;
        case DECAY:
            if (int const sustain_level = ((regs.adsr.fields.sustain_level + 1) << 12);
                volume <= sustain_level) {
                phase = SUSTAIN;
            }
            return false;
        case SUSTAIN:
            if (settings.decrease && volume < 0) {
                volume = 0;
            }
            return false;
        case RELEASE:
            return volume <= 0;
        default:
            std::unreachable();
        };
    }
};

enum class VoiceStatus : uint8_t {
    OFF,
    ON,
    LOOP_END,
};

struct VoiceState {
    std::array<uint64_t, 2> key_on_cycle{};
    std::array<uint64_t, 2> key_off_cycle{};
    SampleBuffer sample_buffer{};
    FilterBuffer filter_buffer{};
    spu_compressed_addr_t sample_p{};
    AdsrState adsr{};
    VoiceStatus status{};
    bool ignore_loop_start{};
};

struct FIR {
    // clang-format off
    static constexpr int16_t COEFFICIENTS[]{
        -0x0001, 0x0000,  0x0002, 0x0000, -0x000a, 0x0000,  0x0023, 0x0000, -0x0067, 0x0000,
         0x010a, 0x0000, -0x0268, 0x0000,  0x0534, 0x0000, -0x0b90, 0x0000,  0x2806, 0x4000,
         0x2806, 0x0000, -0x0b90, 0x0000,  0x0534, 0x0000, -0x0268, 0x0000,  0x010a, 0x0000,
        -0x0067, 0x0000,  0x0023, 0x0000, -0x000a, 0x0000,  0x0002, 0x0000, -0x0001,
    };
    // clang-format on
    static constexpr auto SIZE = std::size(COEFFICIENTS);

    std::array<int16_t, SIZE> buf{};
    size_t p{};

    void push(int16_t sample)
    {
        buf[p++] = sample;
        if (p >= SIZE) {
            p = 0;
        }
    }

    int16_t filter() const
    {
        int32_t o{};
        auto p = this->p;
        for (auto const coeff : COEFFICIENTS) {
            auto const sample = buf[p++];
            if (p >= SIZE) {
                p = 0;
            }
            o += sample * coeff;
        }
        return std::saturating_cast<int16_t>(o >> 15);
    }
};

// Small ad-hoc 16-bit saturanting fixed-point helper type.
struct Fixed16 {
    Fixed16(int16_t i)
        : raw{i}
    {}

    friend Fixed16 operator*(Fixed16 a, Fixed16 b)
    {
        return std::saturating_cast<int16_t>((a.raw * b.raw) >> 15);
    }

    friend Fixed16 operator+(Fixed16 a, Fixed16 b) { return std::saturating_add(a.raw, b.raw); }

    friend Fixed16 operator-(Fixed16 a, Fixed16 b) { return std::saturating_sub(a.raw, b.raw); }

    int16_t raw;
};

struct ReverbState {
    FIR input_filter[2];
    FIR output_filter[2];
    uint32_t current{};
};

struct SPU::Private {
    void write_register(r3000_ptr_t addr, uint16_t value);
    uint16_t read_register(r3000_ptr_t addr);

    uint64_t dma_write(r3000_ptr_t addr, uint32_t nbytes);
    uint64_t dma_read(r3000_ptr_t addr, uint32_t nbytes);

    void update_status();
    void update_key_on_off(uint32_t effective_value, auto member);

    void tick(uint64_t clock_cycle);
    std::pair<int16_t, int16_t> tick_voice(size_t v, uint64_t sample_cycle);
    std::pair<int16_t, int16_t> tick_reverb(std::pair<int16_t, int16_t> input,
                                            uint64_t sample_cycle);

    R3000* emu;
    DMA* dma;
    EmuBuffer<spu_regs_t> reg;

    struct State {
        uint32_t transfer_addr{};
        std::array<VoiceState, N_VOICES> voice{};
        ReverbState reverb{};
    } state{};

    EmuBuffer<uint16_t> system_ram;
    uint64_t last_sample_cycle{};
    Timing* timing;
    std::span<int16_t> out{};
    size_t out_p{};
    alignas(uint32_t) std::array<uint16_t, SPU_RAM_SIZE_WORDS> ram;
};

uint64_t SPU::Private::dma_write(r3000_ptr_t addr, uint32_t nbytes)
{
    addr /= sizeof(uint16_t);
    for (auto i = 0u; i < nbytes; i += sizeof(uint16_t)) {
        ram[state.transfer_addr] = system_ram[addr];
        addr++;
        state.transfer_addr = (state.transfer_addr + 1) % SPU_RAM_SIZE_WORDS;
    }
    return nbytes;
}

uint64_t SPU::Private::dma_read(r3000_ptr_t addr, uint32_t nbytes)
{
    addr /= sizeof(uint16_t);
    for (auto i = 0u; i < nbytes; i += sizeof(uint16_t)) {
        system_ram[addr] = ram[state.transfer_addr];
        addr += sizeof(uint16_t);
        state.transfer_addr = (state.transfer_addr + 1) % SPU_RAM_SIZE_WORDS;
    }
    return nbytes;
}

void SPU::Private::update_status()
{
    auto& status = reg->status;
    auto const& control = reg->control.fields;
    status.fields.cd_audio_en = control.cd_audio_en;
    status.fields.ext_audio_en = control.ext_audio_en;
    status.fields.cd_audio_reverb = control.cd_audio_reverb;
    status.fields.ext_audio_reverb = control.ext_audio_reverb;
    status.fields.transfer_mode = control.transfer_mode;
    status.fields.irq_flag = 0;
    status.fields.dma_req = control.transfer_mode >= 2;
    status.fields.dma_write_req = control.transfer_mode == 2;
    status.fields.dma_read_req = control.transfer_mode == 3;
}

void SPU::Private::update_key_on_off(uint32_t effective_value, auto member)
{
    if (!reg->control.fields.enable) {
        return;
    }

    uint32_t bit;
    auto const sample_cycle = get_sample_cycle(timing->get_clock());
    while ((bit = std::countr_zero(effective_value)) != 32) {
        effective_value &= ~(1u << bit);
        if (bit >= state.voice.size()) {
            break;
        }
        auto& arr = state.voice[bit].*member;
        if (auto const it = std::ranges::find(arr, 0); it != arr.end()) {
            *it = sample_cycle;
        } else {
            arr.back() = sample_cycle;
        }
    }
}

#define start_reg_handling() if (false) {

#define handle_reg(field_name, ...)                                                        \
    }                                                                                      \
    else if (in_range(addr, __VA_ARGS__))                                                  \
    {                                                                                      \
        static_assert(offsetof(spu_regs_t, field_name) == spu_offset(__VA_ARGS__));        \
        static_assert(sizeof(spu_regs_t::field_name) == range_size(__VA_ARGS__));          \
        [[maybe_unused]] static constexpr r3000_ptr_t RANGE_BASE = get_first(__VA_ARGS__); \
        [[maybe_unused]] auto& field_name = reg->field_name;
#define handle_unhandled() \
    }                      \
    else                   \
    {
#define end_reg_handling() }

void SPU::Private::write_register(r3000_ptr_t addr, uint16_t value)
{
    start_reg_handling();
    handle_reg(voice, 0x1f801c00, 0x1f801d80)
    {
        auto const offset = addr - RANGE_BASE;
        auto const voice_n = offset / sizeof(voice[0]);
        auto const index = (offset % sizeof(voice[0])) / sizeof(uint16_t);
        auto& v = voice[voice_n];
        // clang-format off
        switch (index) {
            case 0: v.vol_left.raw             = value;     break;
            case 1: v.vol_right.raw            = value;     break;
            case 2: v.adpcm_sample_rate        = value;     break;
            case 3: v.adpcm_start_addr.raw     = value;     break;
            case 4: write_32bit_reg(&v.adsr.raw, value, 0); break;
            case 5: write_32bit_reg(&v.adsr.raw, value, 2); break;
            case 6: v.adsr_vol                 = value;     break;
            case 7: v.adpcm_repeat_addr.raw    = value;
                    state.voice[voice_n].ignore_loop_start = true;
                    break;
        }
        // clang-format on
    }
    handle_reg(mixer_vol_left, 0x1f801d80)
    {
        mixer_vol_left = value;
    }
    handle_reg(mixer_vol_right, 0x1f801d82)
    {
        mixer_vol_right = value;
    }
    handle_reg(reverb_vol_left, 0x1f801d84)
    {
        reverb_vol_left = value;
    }
    handle_reg(reverb_vol_right, 0x1f801d86)
    {
        reverb_vol_right = value;
    }
    handle_reg(voice_key_on, 0x1f801d88, 0x1f801d8c)
    {
        auto effective_value = write_32bit_reg(&voice_key_on, value, addr - RANGE_BASE);
        update_key_on_off(effective_value, &VoiceState::key_on_cycle);
    }
    handle_reg(voice_key_off, 0x1f801d8c, 0x1f801d90)
    {
        auto effective_value = write_32bit_reg(&voice_key_off, value, addr - RANGE_BASE);
        update_key_on_off(effective_value, &VoiceState::key_off_cycle);
    }
    handle_reg(voice_fmod_en, 0x1f801d90, 0x1f801d94)
    {
        write_32bit_reg(&voice_fmod_en, value, addr - RANGE_BASE);
    }
    handle_reg(voice_noise_mode, 0x1f801d94, 0x1f801d98)
    {
        write_32bit_reg(&voice_noise_mode, value, addr - RANGE_BASE);
    }
    handle_reg(voice_reverb_on, 0x1f801d98, 0x1f801d9c)
    {
        write_32bit_reg(&voice_reverb_on, value, addr - RANGE_BASE);
    }
    handle_reg(reverb_base_addr, 0x1f801da2)
    {
        reverb_base_addr.raw = value;
        state.reverb.current = reverb_base_addr.get();
    }
    handle_reg(irq_addr, 0x1f801da4)
    {
        irq_addr.raw = value;
    }
    handle_reg(transfer_addr, 0x1f801da6)
    {
        transfer_addr.raw = value;
        state.transfer_addr = transfer_addr.get();
    }
    handle_reg(transfer_data, 0x1f801da8)
    {
        ram[state.transfer_addr] = value;
        state.transfer_addr = (state.transfer_addr + 1) % SPU_RAM_SIZE_WORDS;
    }
    handle_reg(control, 0x1f801daa)
    {
        control.raw = value;
        update_status();
        dma->request_transfer(4, reg->status.fields.dma_req);
    }
    handle_reg(transfer_control, 0x1f801dac)
    {
        transfer_control = value;
    }
    handle_reg(status, 0x1f801dae)
    {
        // Read-only, do nothing.
    }
    handle_reg(cd_audio_left, 0x1f801db0)
    {
        cd_audio_left = value;
    }
    handle_reg(cd_audio_right, 0x1f801db2)
    {
        cd_audio_right = value;
    }
    handle_reg(ext_vol_left, 0x1f801db4)
    {
        ext_vol_left = value;
    }
    handle_reg(ext_vol_right, 0x1f801db6)
    {
        ext_vol_right = value;
    }
    handle_reg(reverb_config, 0x1f801dc0, 0x1f801e00)
    {
        reverb_config.r[(addr - RANGE_BASE) / sizeof(uint16_t)] = value;
    }
    handle_reg(voice_current_vol, 0x1f801e00, 0x1f801e60)
    {
        // Read-only registers, do nothing.
    }
    handle_unhandled()
    {
        fmt::println("Unhandled SPU write: {:#010x} = {:#06x}", addr, value);
    }
    end_reg_handling();
}

uint16_t SPU::Private::read_register(r3000_ptr_t addr)
{
    start_reg_handling();
    handle_reg(voice, 0x1f801c00, 0x1f801d80)
    {
        auto const offset = addr - RANGE_BASE;
        auto const voice_n = offset / sizeof(voice[0]);
        auto const index = (offset % sizeof(voice[0])) / sizeof(uint16_t);
        auto& v = voice[voice_n];
        // clang-format off
        switch (index) {
            case 0: return v.vol_left.raw;
            case 1: return v.vol_right.raw;
            case 2: return v.adpcm_sample_rate;
            case 3: return v.adpcm_start_addr.raw;
            case 4: return read_32bit_reg(v.adsr.raw, 0);
            case 5: return read_32bit_reg(v.adsr.raw, 2);
            case 6: return v.adsr_vol;
            case 7: return v.adpcm_repeat_addr.raw;
        }
        // clang-format on
    }
    handle_reg(mixer_vol_left, 0x1f801d80)
    {
        return mixer_vol_left;
    }
    handle_reg(mixer_vol_right, 0x1f801d82)
    {
        return mixer_vol_right;
    }
    handle_reg(reverb_vol_left, 0x1f801d84)
    {
        return reverb_vol_left;
    }
    handle_reg(reverb_vol_right, 0x1f801d86)
    {
        return reverb_vol_right;
    }
    handle_reg(voice_key_on, 0x1f801d88, 0x1f801d8c)
    {
        return read_32bit_reg(voice_key_on, addr - RANGE_BASE);
    }
    handle_reg(voice_key_off, 0x1f801d8c, 0x1f801d90)
    {
        return read_32bit_reg(voice_key_off, addr - RANGE_BASE);
    }
    handle_reg(voice_fmod_en, 0x1f801d90, 0x1f801d94)
    {
        return read_32bit_reg(voice_fmod_en, addr - RANGE_BASE);
    }
    handle_reg(voice_noise_mode, 0x1f801d94, 0x1f801d98)
    {
        return read_32bit_reg(voice_noise_mode, addr - RANGE_BASE);
    }
    handle_reg(voice_reverb_on, 0x1f801d98, 0x1f801d9c)
    {
        return read_32bit_reg(voice_reverb_on, addr - RANGE_BASE);
    }
    handle_reg(reverb_base_addr, 0x1f801da2)
    {
        return reverb_base_addr.raw;
    }
    handle_reg(irq_addr, 0x1f801da4)
    {
        return irq_addr.raw;
    }
    handle_reg(transfer_addr, 0x1f801da6)
    {
        return transfer_addr.raw;
    }
    handle_reg(transfer_data, 0x1f801da8)
    {
        // There is no way to read data "manually", this register always reads as 0xffff.
        return 0xffffu;
    }
    handle_reg(control, 0x1f801daa)
    {
        return control.raw;
    }
    handle_reg(transfer_control, 0x1f801dac)
    {
        return transfer_control;
    }
    handle_reg(status, 0x1f801dae)
    {
        return status.raw;
    }
    handle_reg(cd_audio_left, 0x1f801db0)
    {
        return cd_audio_left;
    }
    handle_reg(cd_audio_right, 0x1f801db2)
    {
        return cd_audio_right;
    }
    handle_reg(ext_vol_left, 0x1f801db4)
    {
        return ext_vol_left;
    }
    handle_reg(ext_vol_right, 0x1f801db6)
    {
        return ext_vol_right;
    }
    handle_reg(reverb_config, 0x1f801dc0, 0x1f801e00)
    {
        return reverb_config.r[(addr - RANGE_BASE) / sizeof(uint16_t)];
    }
    handle_reg(voice_current_vol, 0x1f801e00, 0x1f801e60)
    {
        auto const offset = addr - RANGE_BASE;
        auto const voice_n = offset / sizeof(voice_current_vol[0]);
        auto const index = (offset % sizeof(voice_current_vol[0])) / sizeof(uint16_t);
        auto& v = voice_current_vol[voice_n];
        switch (index) {
        case 0:
            return v.left;
        case 1:
            return v.right;
        }
    }
    handle_unhandled()
    {
        fmt::println("Unhandled SPU read: {:#010x}", addr);
    }
    end_reg_handling();

    return 0;
}

void SPU::Private::tick(uint64_t const clock_cycle)
{
    uint64_t const sample_cycle = get_sample_cycle(clock_cycle);
    uint64_t const samples_to_generate = sample_cycle - last_sample_cycle;
    uint64_t const current_sample_cycle = last_sample_cycle;
    last_sample_cycle = sample_cycle;

    if (!reg->control.fields.enable) {
        return;
    }

    for (auto i = 0u; i < samples_to_generate; i++) {
        std::pair<int32_t, int32_t> sum{};
        std::pair<int32_t, int32_t> reverbSum{};

        for (auto v = 0u; v < N_VOICES; v++) {
            auto const sample = tick_voice(v, current_sample_cycle + i);
            sum.first += sample.first;
            sum.second += sample.second;
            if (reg->voice_reverb_on & (1 << v)) {
                reverbSum.first += sample.first;
                reverbSum.second += sample.second;
            }
        }

        if (reg->control.fields.reverb_en) {
            auto const clamped =
                std::make_pair(smooth_clamp(reverbSum.first), smooth_clamp(reverbSum.second));
            auto const reverb_sample = tick_reverb(clamped, current_sample_cycle + i);
            sum.first += reverb_sample.first;
            sum.second += reverb_sample.second;
        }

        if (out.size() >= out_p + 2) {
            out[out_p] = smooth_clamp(sum.first);
            out[out_p + 1] = smooth_clamp(sum.second);
            out_p += 2;
        }
    }
}

std::pair<int16_t, int16_t> SPU::Private::tick_voice(size_t const v, uint64_t sample_cycle)
{
    auto& voice_regs = reg->voice[v];
    auto& voice_state = state.voice[v];

    if (auto const it = std::ranges::find(voice_state.key_on_cycle, sample_cycle);
        it != voice_state.key_on_cycle.end()) {
        *it = 0;
        voice_state.sample_buffer.reset();
        voice_state.filter_buffer.reset();
        voice_state.adsr.start();
        voice_state.sample_p = voice_regs.adpcm_start_addr;
        voice_state.status = VoiceStatus::ON;
        voice_state.ignore_loop_start = false;
        voice_regs.adsr_vol = 0;
    }

    if (auto const it = std::ranges::find(voice_state.key_off_cycle, sample_cycle);
        it != voice_state.key_off_cycle.end()) {
        *it = 0;
        voice_state.adsr.release();
    }

    if (voice_state.status == VoiceStatus::OFF) {
        return {0, 0};
    }

    auto const adpcm_sample_rate = std::min<uint16_t>(voice_regs.adpcm_sample_rate, 0x4000);
    voice_state.sample_buffer.frac += adpcm_sample_rate;
    while (voice_state.sample_buffer.frac >= 0x1000) {
        if (voice_state.sample_buffer.empty()) {
            if (voice_state.status == VoiceStatus::LOOP_END) {
                voice_state.status = VoiceStatus::OFF;
                voice_regs.adsr_vol = 0;
                return {0, 0};
            }
            auto const header = decode_adpcm_block(ram,
                                                   voice_state.sample_p.get(),
                                                   voice_state.sample_buffer.last_samples(),
                                                   voice_state.sample_buffer.samples);
            voice_state.sample_buffer.loaded_block();
            if (header.loop_start && !voice_state.ignore_loop_start) {
                voice_regs.adpcm_repeat_addr = voice_state.sample_p;
            }
            if (header.loop_end) {
                voice_state.sample_p = voice_regs.adpcm_repeat_addr;
                if (!header.loop_repeat) {
                    voice_state.status = VoiceStatus::LOOP_END;
                }
            } else {
                voice_state.sample_p.raw += 2;
            }
        }

        auto const sample = voice_state.sample_buffer.sample_and_advance();
        voice_state.filter_buffer.push(sample);
        voice_state.sample_buffer.frac -= 0x1000;
    }

    auto filtered_sample =
        voice_state.filter_buffer.get_filtered(voice_state.sample_buffer.get_fractional_index());

    auto const turn_off_voice = voice_state.adsr.tick(voice_regs);
    if (turn_off_voice) {
        voice_state.status = VoiceStatus::OFF;
        voice_regs.adsr_vol = 0;
    }

    filtered_sample = (filtered_sample * int(voice_regs.adsr_vol)) >> 15;

    auto apply_vol = [&](int16_t sample, auto const& vol_reg) -> int16_t {
        if (!vol_reg.sweep_mode.mode) {
            return (int32_t(sample) * vol_reg.direct_mode.volume) >> 14;
        } else {
            return sample;
        }
    };

    return {apply_vol(filtered_sample, voice_regs.vol_left),
            apply_vol(filtered_sample, voice_regs.vol_right)};
}

std::pair<int16_t, int16_t> SPU::Private::tick_reverb(std::pair<int16_t, int16_t> input_raw,
                                                      uint64_t sample_cycle)
{
    auto& reverb_state = state.reverb;
    auto& reverb_regs = reg->reverb_config.n;
    reverb_state.input_filter[0].push(input_raw.first);
    reverb_state.input_filter[1].push(input_raw.second);

    size_t const c = sample_cycle % 2;
    int16_t const input =
        (int32_t(reverb_state.input_filter[c].filter()) * reverb_regs.vol_in[c]) >> 15;

    auto reverb_addr = [&](spu_compressed_addr_t offset_c, uint32_t sub = 0) {
        uint32_t offset =
            (offset_c.get() - sub) % (SPU_RAM_SIZE_WORDS - reg->reverb_base_addr.get());
        size_t out = reverb_state.current + offset;
        if (out >= SPU_RAM_SIZE_WORDS) {
            out = reg->reverb_base_addr.get() + (out - SPU_RAM_SIZE_WORDS);
        }
        return out;
    };

    auto tick_reflection = [&](auto m_addr_c, auto d_addr_c) {
        auto const m_addr = reverb_addr(m_addr_c);
        auto const m_addr_p = reverb_addr(m_addr_c, 1);
        auto const d_addr = reverb_addr(d_addr_c);
        Fixed16 const input_sample = input;
        Fixed16 const d_v = ram[d_addr];
        Fixed16 const m_v = ram[m_addr_p];
        Fixed16 const v_wall = reverb_regs.v_wall;
        Fixed16 const v_iir = reverb_regs.v_iir;
        auto const out = (input_sample + d_v * v_wall - m_v) * v_iir + m_v;
        ram[m_addr] = out.raw;
    };

    auto tick_comb = [&] {
        Fixed16 const v_comb1 = reverb_regs.v_comb1;
        Fixed16 const v_comb2 = reverb_regs.v_comb2;
        Fixed16 const v_comb3 = reverb_regs.v_comb3;
        Fixed16 const v_comb4 = reverb_regs.v_comb4;
        Fixed16 const m_comb1 = ram[reverb_addr(reverb_regs.m_comb1[c])];
        Fixed16 const m_comb2 = ram[reverb_addr(reverb_regs.m_comb2[c])];
        Fixed16 const m_comb3 = ram[reverb_addr(reverb_regs.m_comb3[c])];
        Fixed16 const m_comb4 = ram[reverb_addr(reverb_regs.m_comb4[c])];
        auto const out =
            v_comb1 * m_comb1 + v_comb2 * m_comb2 + v_comb3 * m_comb3 + v_comb4 * m_comb4;
        return out;
    };

    auto tick_all_pass = [&](Fixed16 input_sample, Fixed16 v_apf, auto m_apf_c, auto d_apf_c) {
        auto const ram_out_addr = reverb_addr(m_apf_c);
        auto const ram_in_addr = reverb_addr(m_apf_c, d_apf_c.get());
        Fixed16 const ram_in = ram[ram_in_addr];
        Fixed16 const tmp = input_sample - v_apf * ram_in;
        ram[ram_out_addr] = tmp.raw;
        auto const out = v_apf * tmp + ram_in;
        return out;
    };

    // Same side reflection
    tick_reflection(reverb_regs.m_same[c], reverb_regs.d_same[c]);
    // Different side reflection
    tick_reflection(reverb_regs.m_diff[!c], reverb_regs.d_diff[c]);
    // Comb filter
    auto const comb_out = tick_comb();
    // Two all-pass filters
    auto const apf1_out =
        tick_all_pass(comb_out, reverb_regs.v_apf1, reverb_regs.m_apf1[c], reverb_regs.d_apf1);
    auto const apf2_out =
        tick_all_pass(apf1_out, reverb_regs.v_apf2, reverb_regs.m_apf2[c], reverb_regs.d_apf2);

    reverb_state.output_filter[c].push(apf2_out.raw);
    reverb_state.output_filter[!c].push(0);

    int16_t const out_l = reverb_state.output_filter[0].filter() * 2;
    int16_t const out_r = reverb_state.output_filter[1].filter() * 2;

    if (c) {
        reverb_state.current++;
        if (reverb_state.current >= SPU_RAM_SIZE_WORDS) {
            reverb_state.current = reg->reverb_base_addr.get();
        }
    }

    return {(out_l * reg->reverb_vol_left) >> 15, (out_r * reg->reverb_vol_right) >> 15};
}

void SPU::write_reg(r3000_ptr_t addr, uint16_t value)
{
    _p->write_register(addr, value);
}

uint16_t SPU::read_reg(r3000_ptr_t addr)
{
    return _p->read_register(addr);
}

uint64_t SPU::dma_write(r3000_ptr_t addr, uint32_t nbytes)
{
    return _p->dma_write(addr, nbytes);
}

uint64_t SPU::dma_read(r3000_ptr_t addr, uint32_t nbytes)
{
    return _p->dma_read(addr, nbytes);
}

void SPU::set_output_buffer(std::span<int16_t> output)
{
    _p->out = output;
    _p->out_p = 0;
}

size_t SPU::rendered_samples() const
{
    return _p->out_p / 2;
}

void SPU::init(R3000* emu)
{
    _p->emu = emu;
    _p->dma = emu->get_dma();
    _p->reg = emu->get_device_buffer<spu_regs_t>(SPU_BASE);
    _p->system_ram = emu->get_buffer<uint16_t>(0, 0x100000);
    _p->timing = emu->get_timing();
    _p->timing->schedule(Timing::Event{
        .name = "spu_tick",
        .type = Timing::Event::Type::PERIODIC_NON_STRICT,
        .period = SAMPLE_RATE_CYCLES,
        .callback = [_p = _p.get()](auto&, uint64_t cycle) { _p->tick(cycle); },
    });
}

SPU::SPU()
    : _p{std::make_unique<Private>()}
{}

SPU::~SPU() = default;
