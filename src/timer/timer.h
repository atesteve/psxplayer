// SPDX-FileCopyrightText: 2026 Aitor Esteve Alvarado
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "core/r3000.h"

#include <cstdint>

enum class Timer {
    TIMER_0,
    TIMER_1,
    TIMER_2,
    VSYNC,
};

class TimerHandler {
public:
    explicit TimerHandler();
    ~TimerHandler();

    void init(R3000* emu);

    void write_reg(r3000_ptr_t addr, uint32_t value);
    uint32_t read_reg(r3000_ptr_t addr);

private:
    struct Private;
    std::unique_ptr<Private> _p;
};
