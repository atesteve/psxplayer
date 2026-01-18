// SPDX-FileCopyrightText: 2026 Aitor Esteve Alvarado
// SPDX-License-Identifier: GPL-3.0-only

#include "timer.h"

#include "core/events.h"

struct TimerHandler::Private {
    void init(R3000* emu);

    R3000* emu;
    Timing* timing;
};

void TimerHandler::Private::init(R3000* emu)
{
    this->emu = emu;
    timing = emu->get_timing();
    // No need to keep the handler for this event, it will always be active.
    timing->schedule({
        .name = "vblank-clock",
        .type = Timing::Event::Type::PERIODIC,
        .period = CPU_FREQ / 60,
        .callback = [emu] {
            emu->istat() |= (1 << IRQ::VBLANK);
        }
    });
}

void TimerHandler::write_reg(r3000_ptr_t addr, uint32_t value)
{}

uint32_t TimerHandler::read_reg(r3000_ptr_t addr)
{
    return 0;
}

void TimerHandler::init(R3000* emu)
{
    _p->init(emu);
}

TimerHandler::TimerHandler()
    : _p{std::make_unique<Private>()}
{}
TimerHandler::~TimerHandler() = default;
