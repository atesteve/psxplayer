// SPDX-FileCopyrightText: 2026 Aitor Esteve Alvarado
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "r3000.h"
#include "snapshot.h"

#include <string>
#include <functional>

inline constexpr size_t CPU_FREQ = 33868800;

struct R3000;

class Timing {
public:
    struct Event {
        enum class Type {
            ONE_SHOT,
            PERIODIC,
            PERIODIC_NON_STRICT,
        };

        struct EventCallback {
            template<typename T>
            using fn_ptr = void (*)(T*, uintptr_t, Event&, uint64_t);

            explicit EventCallback() = default;

            template<typename T, typename Fn>
                requires(requires(fn_ptr<T> fn_ptr, Fn&& fn) { fn_ptr = fn; })
            EventCallback(T* ptr, Fn fn)
                : ptr{(void*)ptr}
                , arg{0}
            {
                fn_ptr<T> fn_ptr = fn;
                this->fn = (uintptr_t)fn_ptr;
            }

            template<typename T, typename Fn>
                requires(requires(fn_ptr<T> fn_ptr, Fn&& fn) { fn_ptr = fn; })
            EventCallback(T* ptr, uintptr_t arg, Fn fn)
                : ptr{(void*)ptr}
                , arg{arg}
            {
                fn_ptr<T> fn_ptr = fn;
                this->fn = (uintptr_t)fn_ptr;
            }

            void operator()(Event& event, uint64_t clock) const
            {
                // Uber UB... but it's fine.
                auto fn_cast = (fn_ptr<void>)fn;
                fn_cast(ptr, arg, event, clock);
            }

            void* ptr;
            uintptr_t arg;
            uintptr_t fn;
        };

        static constexpr uint64_t UNINITIALIZED = -1;

        std::string name;
        Type type;
        uint64_t period = UNINITIALIZED;
        uint64_t first_shot = UNINITIALIZED;
        EventCallback callback;
        bool cpu_visible = true;
        bool cancelled = false;
    };

    enum class Handler : uint64_t {
        UNINITIALIZED,
    };

    explicit Timing();
    ~Timing();

    void init(R3000* emu);

    void advance_clock(uint64_t cycles);
    void fast_forward();
    uint64_t get_clock() const;
    void run_events();
    Handler schedule(Event event);
    bool cancel(Handler handler);

    void take_snapshot(SnapshotSerializer& s);
    void restore_snapshot(SnapshotDeserializer& s);

private:
    struct Private;
    std::unique_ptr<Private> _p;
};
