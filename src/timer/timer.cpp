// SPDX-FileCopyrightText: 2026 Aitor Esteve Alvarado
// SPDX-License-Identifier: GPL-3.0-only

#include "timer.h"
#include "timer_registers.h"

#include "core/events.h"

#include <fmt/format.h>

namespace {
constexpr bool in_range(r3000_ptr_t addr, r3000_ptr_t offset)
{
    if (addr < TIMER_BASE || addr >= TIMER_BASE + TIMER_RANGE_SIZE * TIMER_N) {
        return false;
    }
    auto const addr_offset = addr & (TIMER_RANGE_SIZE - 1);
    return offset == addr_offset;
}

#define start_reg_handling() if (false) {
#define handle_reg(field_name)                                                        \
    }                                                                                 \
    else if (in_range(addr, offsetof(timer_regs_t, field_name)))                      \
    {                                                                                 \
        [[maybe_unused]] auto const timer_n = (addr - TIMER_BASE) / TIMER_RANGE_SIZE; \
        [[maybe_unused]] auto& timer = reg[timer_n];                                  \
        [[maybe_unused]] auto& field_name = timer.field_name;
#define end_reg_handling() }

} // namespace

struct TimerHandler::Private {
    void init(R3000* emu);

    void write_reg(r3000_ptr_t addr, uint32_t value);
    uint32_t read_reg(r3000_ptr_t addr);

    R3000* emu;
    Timing* timing;
    EmuBuffer<timer_regs_t> reg;
};

void TimerHandler::Private::init(R3000* emu)
{
    this->emu = emu;
    timing = emu->get_timing();
    // No need to keep the handler for this event, it will always be active.
    timing->schedule({.name = "vblank-clock",
                      .type = Timing::Event::Type::PERIODIC,
                      .period = CPU_FREQ / 60,
                      .callback = [emu] { emu->istat() |= (1 << IRQ::VBLANK); }});
    reg = emu->get_device_buffer<timer_regs_t>(TIMER_BASE, TIMER_N);
    for (auto i = 0u; i < TIMER_N; i++) {
        reg[i].counter = 0;
        reg[i].target = 0;
        reg[i].counter_mode.raw = 0;
        reg[i].counter_mode.fields.irq = 1;
    }
}

void TimerHandler::Private::write_reg(r3000_ptr_t addr, uint32_t value)
{
    start_reg_handling();
    handle_reg(counter)
    {
        counter = value & 0xffffu;
    }
    handle_reg(counter_mode)
    {
        value = value & 0x3ffu;
        value |= counter_mode.raw & 0x1c00u;
        counter_mode.raw = value;
        counter_mode.fields.irq = 1;
        timer.counter = 0;
    }
    handle_reg(target)
    {
        target = value & 0xffffu;
    }
    end_reg_handling();
}

uint32_t TimerHandler::Private::read_reg(r3000_ptr_t addr)
{
    start_reg_handling();
    handle_reg(counter)
    {
        return counter;
    }
    handle_reg(counter_mode)
    {
        auto const counter_mode_raw = counter_mode.raw;
        counter_mode.fields.hit_target = 0;
        counter_mode.fields.hit_overflow = 0;
        return counter_mode_raw;
    }
    handle_reg(target)
    {
        return target;
    }
    end_reg_handling();

    return 0;
}

void TimerHandler::write_reg(r3000_ptr_t addr, uint32_t value)
{
    return _p->write_reg(addr, value);
}

uint32_t TimerHandler::read_reg(r3000_ptr_t addr)
{
    return _p->read_reg(addr);
}

void TimerHandler::init(R3000* emu)
{
    _p->init(emu);
}

TimerHandler::TimerHandler()
    : _p{std::make_unique<Private>()}
{}
TimerHandler::~TimerHandler() = default;
