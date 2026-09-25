// SPDX-FileCopyrightText: 2026 Aitor Esteve Alvarado
// SPDX-License-Identifier: GPL-3.0-only

#include "spu.h"
#include "spu_registers.h"
#include "adpcm.h"
#include "core/dma.h"

#include "gauss.h"

#include <cstddef>
#include <utility>

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

} // namespace

struct SampleBuffer {
    static constexpr auto FRACT_BITS = 12u;

    uint32_t pos{};
    std::array<int16_t, 28> samples{};

    void reset()
    {
        pos = samples.size() << FRACT_BITS;
        std::ranges::fill(samples, 0);
    }

    bool empty() { return (pos >> FRACT_BITS) >= samples.size(); }

    void loaded_block() { pos -= samples.size() << FRACT_BITS; }

    auto last_samples() const
    {
        return std::make_pair(samples[samples.size() - 1], samples[samples.size() - 2]);
    }

    std::optional<int16_t> sample_and_advance(uint16_t amount)
    {
        amount = std::clamp<uint16_t>(amount, 0, 0x4000);
        auto const prev_pos = std::exchange(pos, pos + amount);
        if (prev_pos >> FRACT_BITS != pos >> FRACT_BITS) {
            return samples[prev_pos >> FRACT_BITS];
        } else {
            return std::nullopt;
        }
    }

    auto get_fractional() const { return (pos >> 4) % FRACT_BITS; }
};

struct FilterBuffer {
    uint16_t pos{};
    std::array<int16_t, 4> samples{};

    void reset() { std::ranges::fill(samples, 0); }
    void push(int16_t sample)
    {
        samples[pos] = sample;
        pos = (pos + 1) % samples.size();
    }

    int16_t get(uint8_t fract) const
    {
        auto const table_offset = size_t(fract) * 4;
        int16_t out = 0;

        for (auto i = 0u; i < samples.size(); i++) {
            int32_t const sample = samples[(pos + i) % samples.size()];
            out += (psx_gauss_table[table_offset + i] * sample) >> 15;
        }

        return out;
    }
};

struct VoiceState {
    std::array<uint64_t, 2> key_on_cycle{};
    std::array<uint64_t, 2> key_off_cycle{};
    SampleBuffer sample_buffer{};
    FilterBuffer filter_buffer{};
    spu_compressed_addr_t sample_p{};
    bool on{};
};

struct SPU::Private {
    void write_register(r3000_ptr_t addr, uint16_t value);
    uint16_t read_register(r3000_ptr_t addr);

    void write_ram(size_t addr, uint16_t value) { ram[addr] = value; }

    uint16_t read_ram(size_t addr) { return ram[addr]; }

    uint64_t dma_write(r3000_ptr_t addr, uint32_t nbytes);
    uint64_t dma_read(r3000_ptr_t addr, uint32_t nbytes);

    void update_status();
    void update_key_on_off(uint32_t effective_value, auto member);

    void tick(uint64_t clock_cycle);
    std::pair<int16_t, int16_t> tick_voice(size_t v, uint64_t sample_cycle);

    R3000* emu;
    DMA* dma;
    EmuBuffer<spu_regs_t> reg;

    struct State {
        uint32_t transfer_addr{};
        std::array<VoiceState, N_VOICES> voice{};
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
        write_ram(state.transfer_addr, system_ram[addr]);
        addr++;
        state.transfer_addr = (state.transfer_addr + 1) % SPU_RAM_SIZE_WORDS;
    }
    return nbytes;
}

uint64_t SPU::Private::dma_read(r3000_ptr_t addr, uint32_t nbytes)
{
    addr /= sizeof(uint16_t);
    for (auto i = 0u; i < nbytes; i += sizeof(uint16_t)) {
        system_ram[addr] = read_ram(state.transfer_addr);
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
            case 7: v.adpcm_repeat_addr.raw    = value;     break;
        }
        // clang-format on
    }
    handle_reg(vol_left, 0x1f801d80)
    {
        vol_left.raw = value;
    }
    handle_reg(vol_right, 0x1f801d82)
    {
        vol_right.raw = value;
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
    handle_reg(voice_reberv_on, 0x1f801d98, 0x1f801d9c)
    {
        write_32bit_reg(&voice_reberv_on, value, addr - RANGE_BASE);
    }
    handle_reg(reberv_base_addr, 0x1f801da2)
    {
        reberv_base_addr.raw = value;
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
        write_ram(state.transfer_addr, value);
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
    handle_reg(vol_left, 0x1f801d80)
    {
        return vol_left.raw;
    }
    handle_reg(vol_right, 0x1f801d82)
    {
        return vol_right.raw;
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
    handle_reg(voice_reberv_on, 0x1f801d98, 0x1f801d9c)
    {
        return read_32bit_reg(voice_reberv_on, addr - RANGE_BASE);
    }
    handle_reg(reberv_base_addr, 0x1f801da2)
    {
        return reberv_base_addr.raw;
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
        for (auto v = 0u; v < N_VOICES; v++) {
            auto const sample = tick_voice(v, current_sample_cycle + i);
            sum.first += sample.first;
            sum.second += sample.second;
        }
        // Very crude mixing.
        if (out.size() >= out_p + 2) {
            out[out_p] = sum.first / 2;
            out[out_p + 1] = sum.second / 2;
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
        voice_state.sample_p = voice_regs.adpcm_start_addr;
        voice_state.on = true;
    }

    if (auto const it = std::ranges::find(voice_state.key_off_cycle, sample_cycle);
        it != voice_state.key_off_cycle.end()) {
        *it = 0;
        voice_state.on = false;
    }

    if (!voice_state.on) {
        return {0, 0};
    }

    if (voice_state.sample_buffer.empty()) {
        auto const header = decode_adpcm_block(ram,
                                               voice_state.sample_p.get(),
                                               voice_state.sample_buffer.last_samples(),
                                               voice_state.sample_buffer.samples);
        voice_state.sample_buffer.loaded_block();
        if (header.loop_start) {
            voice_regs.adpcm_repeat_addr = voice_state.sample_p;
        }
        if (header.loop_end) {
            voice_state.sample_p = voice_regs.adpcm_repeat_addr;
            if (!header.loop_repeat) {
                voice_state.on = false;
                return {0, 0};
            }
        } else {
            voice_state.sample_p.raw += 2;
        }
    }

    auto const sample = voice_state.sample_buffer.sample_and_advance(voice_regs.adpcm_sample_rate);
    if (sample) {
        voice_state.filter_buffer.push(*sample);
    }
    auto const filtered_sample =
        voice_state.filter_buffer.get(voice_state.sample_buffer.get_fractional());

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
