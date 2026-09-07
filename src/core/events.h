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
        uint64_t first_shot = UNINITIALIZED;
        std::move_only_function<void(Event&,uint64_t)> callback;
        bool cancelled = false;
    };

    enum class Handler : uint64_t {
        UNINITIALIZED,
    };

    explicit Timing();
    ~Timing();

    void init(R3000* emu);

    void advance_clock(uint64_t cycles);
    uint64_t get_clock() const;
    void run_events();
    Handler schedule(Event event);
    bool cancel(Handler handler);

private:
    struct Private;
    std::unique_ptr<Private> _p;
};
