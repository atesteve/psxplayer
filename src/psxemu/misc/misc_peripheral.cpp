// SPDX-FileCopyrightText: 2026 Aitor Esteve Alvarado
// SPDX-License-Identifier: GPL-3.0-only

#include "misc_peripheral.h"

#include <fmt/format.h>

namespace {
constexpr r3000_ptr_t SPU_DELAY = 0x1f801014;
constexpr r3000_ptr_t MDEC_COMMAND_DATA = 0x1f801820;
constexpr r3000_ptr_t MDEC_CONTROL_STATUS = 0x1f801824;
constexpr r3000_ptr_t GPU_GP0 = 0x1f801810;
constexpr r3000_ptr_t GPU_GP1 = 0x1f801814;
} // namespace

void Misc::write_reg(r3000_ptr_t addr, uint32_t value)
{
    switch (addr) {
    case SPU_DELAY:
    case MDEC_COMMAND_DATA:
    case MDEC_CONTROL_STATUS:
    case GPU_GP0:
    case GPU_GP1:
        // Just ignore all of them.
        break;
    default:
        fmt::println("Write to unknown reg: {:#010x} = {:#010x}", addr, value);
    }
}

uint32_t Misc::read_reg(r3000_ptr_t addr)
{
    switch (addr) {
    case SPU_DELAY:
    case MDEC_COMMAND_DATA:
    case GPU_GP0:
        // Just ignore all of them.
        return 0;
        break;

    case GPU_GP1:
        // Return this magic value as "initialized"
        return 0x14802000;

    case MDEC_CONTROL_STATUS:
        // Return this magic value as "initialized"
        return 0x80000000;

    default:
        fmt::println("Read from unknown reg: {:#010x}", addr);
        return 0;
    }
}
