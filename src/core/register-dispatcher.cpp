#include "register-dispatcher.h"
#include "dma.h"
#include "timer/timer.h"
#include "spu/spu.h"

#include <fmt/format.h>

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

template<std::integral Int, typename Fn, typename T>
void fix_32bit_write(Fn&& fn, T* p, uint32_t addr, Int value)
{
    if constexpr (std::is_same_v<Int, uint32_t>) {
        (*p.*fn)(addr, value);
    } else {
        (*p.*fn)(addr & ~3u, uint32_t(value) << ((addr & 3u) * 8));
    }
}

template<std::integral Int, typename Fn, typename T>
Int fix_32bit_read(Fn&& fn, T* p, uint32_t addr)
{
    if constexpr (std::is_same_v<Int, uint32_t>) {
        return (*p.*fn)(addr);
    } else {
        uint32_t ret = (*p.*fn)(addr & ~3u);
        return ret >> ((addr & 3u) * 8);
    }
}

template<std::integral Int>
auto fix_32bit_address(uint32_t addr)
{
    if constexpr (std::is_same_v<Int, uint32_t>) {
        return addr;
    } else {
        return addr & ~3u;
    }
}

template<std::integral Int>
void RegisterDispatcher::write_reg(r3000_ptr_t addr, Int value) const
{
    if (addr == HWReg::ISTAT) {
        if constexpr (std::is_same_v<Int, uint8_t>) {
            return;
        }
        auto& istat = emu->istat();
        istat &= value;
    } else if (addr == HWReg::IMASK) {
        if constexpr (std::is_same_v<Int, uint8_t>) {
            return;
        }
        auto& imask = emu->imask();
        imask = value;
    } else if (addr >= HWReg::DMA_start && addr < HWReg::DMA_end) {
        fix_32bit_write(&DMA::write_reg, dma, addr, value);
    } else if (addr >= HWReg::SPU_start && addr < HWReg::SPU_end) {
        if constexpr (!std::is_same_v<Int, uint16_t>) {
            return;
        }
        spu->write_reg(addr, value);
    } else if (addr >= HWReg::Timers_start && addr < HWReg::Timers_end) {
        fix_32bit_write(&TimerHandler::write_reg, timers, addr, value);
    } else if (addr == HWReg::SPU_DELAY) {
        // Just write it, do nothing.
        fix_32bit_write(&RegisterDispatcher::write<uint32_t>, this, addr, value);
    } else {
        fmt::println("Write to unknown reg: {:#010x} = {:#010x}", addr, value);
        write<Int>(addr, value);
    }
}

template<std::integral Int>
Int RegisterDispatcher::read_reg(r3000_ptr_t addr) const
{
    if (addr == HWReg::ISTAT) {
        if constexpr (std::is_same_v<Int, uint8_t>) {
            return 0;
        }
        return emu->istat();
    } else if (addr == HWReg::IMASK) {
        if constexpr (std::is_same_v<Int, uint8_t>) {
            return 0;
        }
        return emu->imask();
    } else if (addr >= HWReg::DMA_start && addr < HWReg::DMA_end) {
        return fix_32bit_read<Int>(&DMA::read_reg, dma, addr);
    } else if (addr >= HWReg::SPU_start && addr < HWReg::SPU_end) {
        if constexpr (!std::is_same_v<Int, uint16_t>) {
            return 0;
        }
        return spu->read_reg(addr);
    } else if (addr >= HWReg::Timers_start && addr < HWReg::Timers_end) {
        return fix_32bit_read<Int>(&TimerHandler::read_reg, timers, addr);
    } else if (addr == HWReg::SPU_DELAY) {
        // Just read it, do nothing.
        return fix_32bit_read<Int>(&RegisterDispatcher::read<uint32_t>, this, addr);
    } else {
        fmt::println("Read from unknown reg: {:#010x} = {:#010x}", addr, read<Int>(addr));
        return read<Int>(addr);
    }
}

void RegisterDispatcher::init(R3000* emu)
{
    this->emu = emu;
    spu = emu->get_spu();
    dma = emu->get_dma();
    timers = emu->get_timers();
}

template void RegisterDispatcher::write_reg(r3000_ptr_t, uint32_t) const;
template void RegisterDispatcher::write_reg(r3000_ptr_t, uint16_t) const;
template void RegisterDispatcher::write_reg(r3000_ptr_t, uint8_t) const;

template uint32_t RegisterDispatcher::read_reg(r3000_ptr_t) const;
template uint16_t RegisterDispatcher::read_reg(r3000_ptr_t) const;
template uint8_t RegisterDispatcher::read_reg(r3000_ptr_t) const;
