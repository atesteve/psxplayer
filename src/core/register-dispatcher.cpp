#include "register-dispatcher.h"

template<std::integral Int>
Int RegisterDispatcher::read(uint32_t addr) const
{
    return *(Int*)(_mem_space + addr);
}

template<std::integral Int>
void RegisterDispatcher::write(uint32_t addr, Int value) const
{
    *(Int*)(_mem_space + addr) = value;
}

template<std::integral Int>
void RegisterDispatcher::write_reg(R3000& emu, r3000_ptr_t addr, Int value) const
{
    switch (addr) {
    case HWReg::ISTAT: {
        if constexpr (std::is_same_v<Int, uint8_t>) {
            return;
        }
        auto& istat = emu.istat();
        istat &= value;
        return;
    }

    case HWReg::IMASK: {
        if constexpr (std::is_same_v<Int, uint8_t>) {
            return;
        }
        auto& imask = emu.imask();
        imask = value;
        return;
    }
    }

    write<Int>(addr, value);
}

template<std::integral Int>
Int RegisterDispatcher::read_reg(R3000& emu, r3000_ptr_t addr) const
{
    switch (addr) {
    case HWReg::ISTAT: {
        if constexpr (std::is_same_v<Int, uint8_t>) {
            return 0;
        }
        return emu.istat();
    }

    case HWReg::IMASK: {
        if constexpr (std::is_same_v<Int, uint8_t>) {
            return 0;
        }
        return emu.imask();
    }
    }

    return read<Int>(addr);
}

template void RegisterDispatcher::write_reg(R3000&, r3000_ptr_t, uint32_t) const;
template void RegisterDispatcher::write_reg(R3000&, r3000_ptr_t, uint16_t) const;
template void RegisterDispatcher::write_reg(R3000&, r3000_ptr_t, uint8_t) const;

template uint32_t RegisterDispatcher::read_reg(R3000&, r3000_ptr_t) const;
template uint16_t RegisterDispatcher::read_reg(R3000&, r3000_ptr_t) const;
template uint8_t RegisterDispatcher::read_reg(R3000&, r3000_ptr_t) const;