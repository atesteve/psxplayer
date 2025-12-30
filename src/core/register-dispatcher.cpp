#include "register-dispatcher.h"

template<std::integral Int>
Int RegisterDispatcher::read(uint32_t addr) const
{
    return *(Int*)(_mem_space + (addr - HWReg::DEVICE_BASE));
}

template<std::integral Int>
void RegisterDispatcher::write(uint32_t addr, Int value) const
{
    *(Int*)(_mem_space + (addr - HWReg::DEVICE_BASE)) = value;
}

template<std::integral Int>
void RegisterDispatcher::write_reg(R3000& emu, r3000_ptr_t addr, Int value) const
{
    if (addr == HWReg::ISTAT) {
        if constexpr (std::is_same_v<Int, uint8_t>) {
            return;
        }
        auto& istat = emu.istat();
        istat &= value;
    } else if (addr == HWReg::IMASK) {
        if constexpr (std::is_same_v<Int, uint8_t>) {
            return;
        }
        auto& imask = emu.imask();
        imask = value;
    } else if (addr >= HWReg::DMA_start && addr < HWReg::DMA_end) {
        if constexpr (!std::is_same_v<Int, uint32_t>) {
            return;
        }
        emu.write_dma_reg(addr, value);
    } else if (addr >= HWReg::SPU_start && addr < HWReg::SPU_end) {
        if constexpr (!std::is_same_v<Int, uint16_t>) {
            return;
        }
        emu.write_spu_reg(addr, value);
    } else {
        write<Int>(addr, value);
    }
}

template<std::integral Int>
Int RegisterDispatcher::read_reg(R3000& emu, r3000_ptr_t addr) const
{
    if (addr == HWReg::ISTAT) {
        if constexpr (std::is_same_v<Int, uint8_t>) {
            return 0;
        }
        return emu.istat();
    } else if (addr == HWReg::IMASK) {
        if constexpr (std::is_same_v<Int, uint8_t>) {
            return 0;
        }
        return emu.imask();
    } else if (addr >= HWReg::DMA_start && addr < HWReg::DMA_end) {
        if constexpr (!std::is_same_v<Int, uint32_t>) {
            return 0;
        }
        return emu.read_dma_reg(addr);
    } else if (addr >= HWReg::SPU_start && addr < HWReg::SPU_end) {
        if constexpr (!std::is_same_v<Int, uint16_t>) {
            return 0;
        }
        return emu.read_spu_reg(addr);
    }else {
        return read<Int>(addr);
    }
}

template void RegisterDispatcher::write_reg(R3000&, r3000_ptr_t, uint32_t) const;
template void RegisterDispatcher::write_reg(R3000&, r3000_ptr_t, uint16_t) const;
template void RegisterDispatcher::write_reg(R3000&, r3000_ptr_t, uint8_t) const;

template uint32_t RegisterDispatcher::read_reg(R3000&, r3000_ptr_t) const;
template uint16_t RegisterDispatcher::read_reg(R3000&, r3000_ptr_t) const;
template uint8_t RegisterDispatcher::read_reg(R3000&, r3000_ptr_t) const;
