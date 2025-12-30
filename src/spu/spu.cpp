#include "spu.h"
#include "spu_registers.h"

#include <cstddef>

namespace {
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

void write_32bit_reg(uint32_t* reg, uint16_t value, r3000_ptr_t offset)
{
    if (offset == 0) {
        *reg = (*reg & 0xffff0000) | value;
    } else {
        *reg = (*reg & 0x0000ffff) | (uint32_t(value) << 16);
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

} // namespace

struct SPU::Private {
    void write_register(r3000_ptr_t addr, uint16_t value);
    uint16_t read_register(r3000_ptr_t addr);

    EmuBuffer<spu_regs_t> reg;
};

SPU::SPU()
    : _p{std::make_unique<Private>()}
{}

SPU::~SPU() = default;

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
            case 0: v.vol_left.bits        = value;     break;
            case 1: v.vol_right.bits       = value;     break;
            case 2: v.adpcm_sample_rate    = value;     break;
            case 3: v.adpcm_start_addr     = value;     break;
            case 4: write_32bit_reg(&v.adsr, value, 0); break;
            case 5: write_32bit_reg(&v.adsr, value, 2); break;
            case 6: v.adsr_vol             = value;     break;
            case 7: v.adsr_repeat_addr     = value;     break;
        }
        // clang-format on
    }
    handle_reg(vol_left, 0x1f801d80)
    {
        vol_left.bits = value;
    }
    handle_reg(vol_right, 0x1f801d82)
    {
        vol_right.bits = value;
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
        write_32bit_reg(&voice_key_on, value, addr - RANGE_BASE);
    }
    handle_reg(voice_key_off, 0x1f801d8c, 0x1f801d90)
    {
        write_32bit_reg(&voice_key_off, value, addr - RANGE_BASE);
    }
    handle_reg(voice_pitch_mod_en, 0x1f801d90, 0x1f801d94)
    {
        write_32bit_reg(&voice_pitch_mod_en, value, addr - RANGE_BASE);
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
        reberv_base_addr = value;
    }
    handle_reg(irq_addr, 0x1f801da4)
    {
        irq_addr = value;
    }
    handle_reg(transfer_addr, 0x1f801da6)
    {
        transfer_addr = value;
    }
    handle_reg(transfer_data, 0x1f801da8)
    {
        transfer_data = value;
    }
    handle_reg(control, 0x1f801daa)
    {
        control = value;
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
            case 0: return v.vol_left.bits;
            case 1: return v.vol_right.bits;
            case 2: return v.adpcm_sample_rate;
            case 3: return v.adpcm_start_addr;
            case 4: return read_32bit_reg(v.adsr, 0);
            case 5: return read_32bit_reg(v.adsr, 2);
            case 6: return v.adsr_vol;
            case 7: return v.adsr_repeat_addr;
        }
        // clang-format on
    }
    handle_reg(vol_left, 0x1f801d80)
    {
        return vol_left.bits;
    }
    handle_reg(vol_right, 0x1f801d82)
    {
        return vol_right.bits;
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
    handle_reg(voice_pitch_mod_en, 0x1f801d90, 0x1f801d94)
    {
        return read_32bit_reg(voice_pitch_mod_en, addr - RANGE_BASE);
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
        return reberv_base_addr;
    }
    handle_reg(irq_addr, 0x1f801da4)
    {
        return irq_addr;
    }
    handle_reg(transfer_addr, 0x1f801da6)
    {
        return transfer_addr;
    }
    handle_reg(transfer_data, 0x1f801da8)
    {
        return transfer_data;
    }
    handle_reg(control, 0x1f801daa)
    {
        return control;
    }
    handle_reg(transfer_control, 0x1f801dac)
    {
        return transfer_control;
    }
    handle_reg(status, 0x1f801dae)
    {
        return status;
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

void SPU::write_register(r3000_ptr_t addr, uint16_t value)
{
    _p->write_register(addr, value);
}

uint16_t SPU::read_register(r3000_ptr_t addr)
{
    return _p->read_register(addr);
}

void SPU::init(R3000* emu)
{
    _p->reg = emu->get_device_buffer<spu_regs_t>(SPU_BASE);
}
