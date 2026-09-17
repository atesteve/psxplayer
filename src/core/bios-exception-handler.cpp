// SPDX-FileCopyrightText: 2026 Aitor Esteve Alvarado
// SPDX-License-Identifier: GPL-3.0-only

#include "bios.h"

#include "core/events.h"

#include <array>

namespace {

constexpr std::array TIMER_IRQ_MASK{
    1u << IRQ::TMR0,
    1u << IRQ::TMR1,
    1u << IRQ::TMR2,
    1u << IRQ::VBLANK,
};

}

void Bios::exception_handler(R3000& emu)
{
    auto const& cp0 = emu.cp0_regs();

    state.saved_state = {
        emu.core(),
        cp0.Status.fields,
    };
    state.saved_state.core.pc = cp0.EPC;

    for (auto const& handlers : state.irq_handlers) {
        for (auto const& handler : handlers) {
            if (!handler.verifier) {
                continue;
            }
            auto const result = handler.verifier(emu);
            if (result == 0) {
                continue;
            }
            if (!handler.handler) {
                continue;
            }
            handler.handler(emu, result);
        }
    }

    if (state.unhandled_irq_farjmp) {
        // timing->advance_clock(total_cycles + 20);
        longjmp(emu, state.unhandled_irq_farjmp, 1);
    }

    emu.core() = state.saved_state.core;
    emu.cp0_regs().Status.fields = state.saved_state.sr;
    emu.return_from_exception();
}

void Bios::init_exception_handlers()
{
    state.irq_handlers[0].emplace_back(IRQHandler{
        .verifier = [this](R3000& emu) { return syscall_verifier(emu); },
    });

    for (auto timer = 0u; timer < 4; timer++) {
        state.irq_handlers[1].emplace_back(IRQHandler{
            .verifier = [this, timer](R3000& emu) { return timer_verifier(emu, timer); },
            .handler = [this, timer](R3000& emu, uint32_t) { return timer_handler(emu, timer); },
        });
    }

    state.irq_handlers[3].emplace_back(IRQHandler{
        .verifier = [this](R3000& emu) { return irq_verifier(emu); },
    });
}

uint32_t Bios::syscall_verifier(R3000& emu)
{
    auto& core = emu.core();
    auto& cp0 = emu.cp0_regs();
    switch (cp0.Cause.fields.ExcCode) {
    case ExceptionCode::Interrupt:
        return 0;

    case ExceptionCode::Syscall:
        switch (core.gpr.n.a0) {
        case 0: // Do nothing.
            break;
        case 1: // enterCriticalSection
            state.saved_state.sr.IEp = 0;
            state.saved_state.sr.IntMask &= ~4;
            break;
        case 2: // leaveCriticalSection
            state.saved_state.sr.IEp = 1;
            state.saved_state.sr.IntMask |= 4;
            break;
        case 3: // do nothing - unimplemented in the original bios
            core.gpr.n.v0 = 1;
            break;
        default:
            deliverEvent(emu, 0xf0000010, 0x4000);
            break;
        }
        returnFromException(emu);

    default:; // Just fall through.
    }

    deliverEvent(emu, 0xf0000010, 0x1000);

    if (state.unhandled_irq_farjmp) {
        longjmp(emu, state.unhandled_irq_farjmp, 1);
    }

    return 0;
}

uint32_t Bios::timer_verifier(R3000& emu, uint32_t timer)
{
    auto const irq_mask = TIMER_IRQ_MASK[timer];
    if ((emu.imask() & irq_mask) == 0 || (emu.istat() & irq_mask) == 0) {
        return 0;
    }
    deliverEvent(emu, 0xf2000000u | timer, 2);
    return 1;
}

void Bios::timer_handler(R3000& emu, uint32_t timer)
{
    // TODO: check timersAutoAck
    auto const irq_mask = TIMER_IRQ_MASK[timer];
    emu.istat() &= ~irq_mask;
    returnFromException(emu);
}

uint32_t Bios::irq_verifier(R3000& emu)
{
    uint64_t total_cycles = 100;
    auto istat = emu.istat();
    auto* const timing = emu.get_timing();

    while (istat) {
        unsigned const bit = std::countr_zero(istat);
        timing->advance_clock(total_cycles + 20);
        total_cycles = 0;
        deliverEvent(emu, 0xf0000000u | (1 << bit), 0x1000);
        istat &= ~(1u << bit);
        if (bit < std::size(state.irq_auto_ack) && state.irq_auto_ack[bit]) {
            emu.istat() &= ~(1u << bit);
        }
    }

    return 0;
}
