// SPDX-FileCopyrightText: 2026 Aitor Esteve Alvarado
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "core/r3000.h"

#include <cstdint>

// clang-format off

inline constexpr r3000_ptr_t TIMER_BASE = 0x1f801100;
inline constexpr r3000_ptr_t TIMER_RANGE_SIZE = 0x10;
inline constexpr r3000_ptr_t TIMER_N = 3;
inline constexpr uint32_t RESET_MODE_OVERFLOW = 0;
inline constexpr uint32_t RESET_MODE_TARGET   = 1;

union timer_counter_mode_t {
    uint32_t raw;
    struct {
        uint32_t sync_enable     : 1;
        uint32_t sync_mode       : 2;
        uint32_t reset_mode      : 1;
        uint32_t irq_on_target   : 1;
        uint32_t irq_on_overflow : 1;
        uint32_t irq_repeat_mode : 1;
        uint32_t irq_toggle_mode : 1;
        uint32_t clock_source    : 2;
        uint32_t irq             : 1;
        uint32_t hit_target      : 1;
        uint32_t hit_overflow    : 1;
        uint32_t _               : 19;
    } fields;
};

static_assert(sizeof(timer_counter_mode_t) == sizeof(uint32_t));

struct timer_regs_t {
    uint32_t counter;
    timer_counter_mode_t counter_mode;
    uint32_t target;
    uint32_t _;
};

static_assert(sizeof(timer_regs_t) == TIMER_RANGE_SIZE);
