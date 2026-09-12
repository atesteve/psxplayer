// SPDX-FileCopyrightText: 2026 Aitor Esteve Alvarado
// SPDX-License-Identifier: GPL-3.0-only

#include "bios.h"

#include "core/events.h"

void Bios::exception_handler(R3000& emu)
{
    state.saved_core = emu.core();
    state.saved_core.pc = emu.cp0_regs().epc;

    uint64_t total_cycles = 100;
    auto istat = emu.istat();
    auto* const timing = emu.get_timing();

    while (istat) {
        auto const bit = std::countr_zero(istat);
        istat &= ~(1u << bit);
        timing->advance_clock(total_cycles + 20);
        total_cycles = 0;
        deliverEvent(emu, 0xf0000000u | (1 << bit), 0x1000);
    }

    if (state.unhanled_irq_farjmp) {
        timing->advance_clock(total_cycles + 20);
        longjmp(emu, state.unhanled_irq_farjmp, 1);
    }

    emu.core() = state.saved_core;
    emu.return_from_exception();
}
