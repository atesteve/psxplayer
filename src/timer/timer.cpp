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

uint64_t get_clock_divider(timer_regs_t& timer, uint32_t n)
{
    auto const clock_source = timer.counter_mode.fields.clock_source;
    switch (n) {
    case 0: // Timer0
        if (clock_source == 1 || clock_source == 3) {
            return 5; // Pixel clock
        }
        break;
    case 1: // Timer1
        if (clock_source == 1 || clock_source == 3) {
            return 15780; // Hblank
        }
        break;
    case 2: // Timer2
        if (clock_source == 2 || clock_source == 3) {
            return 8; // System clock / 8
        }
    }

    // System clock
    return 1;
}

struct timing_info_t {
    Timing::Handler handler{};
    uint64_t sync_clock{};
    uint64_t clock_divider{1};
};

enum class EventType {
    OVERFLOW,
    TARGET,
};

constexpr std::array timer_irq{
    IRQ::TMR0,
    IRQ::TMR1,
    IRQ::TMR2,
};

} // namespace

struct TimerHandler::Private {
    void init(R3000* emu);

    void write_reg(r3000_ptr_t addr, uint32_t value);
    uint32_t read_reg(r3000_ptr_t addr);

    void update_timer_event(timer_regs_t& timer, uint32_t timer_n);
    void timer_event(timer_regs_t& timer,
                     uint32_t timer_n,
                     uint64_t current_clk,
                     EventType event_type);
    void vblank_event();

    R3000* emu;
    Timing* timing;
    EmuBuffer<timer_regs_t> reg;

    std::array<timing_info_t, TIMER_N> timing_info{};
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
        .callback = [this](auto&, auto) { vblank_event(); },
    });
    reg = emu->get_device_buffer<timer_regs_t>(TIMER_BASE, TIMER_N);
    for (auto i = 0u; i < TIMER_N; i++) {
        reg[i].counter = 0;
        reg[i].target = 0;
        reg[i].counter_mode.raw = 0;
        reg[i].counter_mode.fields.irq = 1;
    }
}

void TimerHandler::Private::update_timer_event(timer_regs_t& timer, uint32_t timer_n)
{
    auto& timing_info = this->timing_info[timer_n];
    auto& counter_mode = timer.counter_mode.fields;

    timing->cancel(timing_info.handler);
    timing_info.handler = Timing::Handler::UNINITIALIZED;

    timing_info.sync_clock = timing->get_clock();
    timing_info.clock_divider = get_clock_divider(timer, timer_n);

    if (counter_mode.reset_mode == RESET_MODE_OVERFLOW) {
        if (!counter_mode.irq_on_overflow) {
            return;
        }
        timing_info.handler = timing->schedule({
            .name = fmt::format("timer{}", timer_n),
            .type = Timing::Event::Type::PERIODIC,
            .period = 0x10000 * timing_info.clock_divider,
            .first_shot = (0x10000 - timer.counter) * timing_info.clock_divider,
            .callback =
                [this, &timer, timer_n](auto&, uint64_t current_clk) {
                    timer_event(timer, timer_n, current_clk, EventType::OVERFLOW);
                },
        });
    } else {
        bool const first_overflow = timer.counter > timer.target;
        auto const first_shot = ((first_overflow ? 0x10000 : timer.target + 1) - timer.counter)
            * timing_info.clock_divider;

        if (!counter_mode.irq_on_target) {
            if (first_overflow) {
                timing_info.handler = timing->schedule({
                    .name = fmt::format("timer{}", timer_n),
                    .type = Timing::Event::Type::ONE_SHOT,
                    .first_shot = first_shot,
                    .callback =
                        [this, &timer, timer_n](auto&, uint64_t current_clk) {
                            timer_event(timer, timer_n, current_clk, EventType::OVERFLOW);
                        },
                });
            }
            return;
        }
        timing_info.handler = timing->schedule({
            .name = fmt::format("timer{}", timer_n),
            .type = Timing::Event::Type::PERIODIC,
            .period = (timer.target + 1) * timing_info.clock_divider,
            .first_shot = first_shot,
            .callback =
                [this, &timer, timer_n, first_overflow = first_overflow](
                    auto&, uint64_t current_clk) mutable {
                    timer_event(timer,
                                timer_n,
                                current_clk,
                                first_overflow ? EventType::OVERFLOW : EventType::TARGET);
                    first_overflow = false;
                },
        });
    }
}

void TimerHandler::Private::timer_event(timer_regs_t& timer,
                                        uint32_t timer_n,
                                        uint64_t current_clk,
                                        EventType event_type)
{
    auto& counter_mode = timer.counter_mode.fields;
    auto const irq = timer_irq[timer_n];

    timing_info[timer_n].sync_clock = current_clk;
    timer.counter = 0;

    if (event_type == EventType::OVERFLOW && !counter_mode.irq_on_overflow) {
        return;
    }
    if (event_type == EventType::TARGET && !counter_mode.irq_on_target) {
        return;
    }

    if (counter_mode.irq_toggle_mode) {
        counter_mode.irq = !counter_mode.irq;
        if (!counter_mode.irq) {
            emu->istat() |= (1 << irq);
        }
    }
    else if (counter_mode.irq_repeat_mode) {
        counter_mode.irq = 1;
        emu->istat() |= (1 << irq);
    }
    else {
        if (counter_mode.irq) {
            emu->istat() |= (1 << irq);
        }
        counter_mode.irq = 0;
    }
}

void TimerHandler::Private::vblank_event()
{
    emu->istat() |= (1 << IRQ::VBLANK);

    // Handle timer 1 sync modes
    auto& timer1 = reg[1];
    if (timer1.counter_mode.fields.sync_enable) {
        switch (timer1.counter_mode.fields.sync_mode) {
        case 0:
            break; // Unhandled.

        case 1:
        case 2:
            timer1.counter = 0;
            update_timer_event(timer1, 1);
            break;

        case 3:
            timer1.counter = 0;
            timer1.counter_mode.fields.sync_enable = 0;
            update_timer_event(timer1, 1);
            break;
        }
    }
}

void TimerHandler::Private::write_reg(r3000_ptr_t addr, uint32_t value)
{
    start_reg_handling();
    handle_reg(counter)
    {
        counter = value & 0xffffu;
        update_timer_event(timer, timer_n);
    }
    handle_reg(counter_mode)
    {
        value = value & 0x3ffu;
        value |= counter_mode.raw & 0x1c00u;
        counter_mode.raw = value;
        counter_mode.fields.irq = 1;
        timer.counter = 0;
        update_timer_event(timer, timer_n);
    }
    handle_reg(target)
    {
        target = value & 0xffffu;
        if (timer.counter_mode.fields.reset_mode == RESET_MODE_TARGET) {
            update_timer_event(timer, timer_n);
        }
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
