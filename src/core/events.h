// SPDX-FileCopyrightText: 2026 Aitor Esteve Alvarado
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "r3000.h"

#include <string>
#include <functional>

inline constexpr size_t CPU_FREQ = 33868800;

class Timing {
public:
    struct Event {
        enum class Type {
            ONE_SHOT,
            PERIODIC,
        };

        static constexpr uint64_t UNINITIALIZED = -1;

        std::string name;
        Type type;
        uint64_t period = UNINITIALIZED;
        uint64_t phase = UNINITIALIZED;
        std::move_only_function<void()> callback;
    };

    enum class Handler : uint64_t {};

    explicit Timing();
    ~Timing();

    void init(R3000* emu);

    void advance_clock(uint64_t cycles);
    uint64_t get_clock() const;
    void run_events();
    Handler schedule(Event event);

private:
    struct Private;
    std::unique_ptr<Private> _p;
};
