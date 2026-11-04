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
        };

        struct EventCallback {
            using fn_ptr = uint64_t (*)(void*, uintptr_t, Event&, uint64_t, uint64_t);

            explicit EventCallback() = default;

            template<typename T, typename Fn>
            EventCallback(T* ptr, Fn)
                : ptr{(void*)ptr}
                , arg{0}
            {
                set_fn<T, Fn>();
            }

            template<typename T, typename Fn>
            EventCallback(T* ptr, uintptr_t arg, Fn)
                : ptr{(void*)ptr}
                , arg{arg}
            {
                set_fn<T, Fn>();
            }

            uint64_t operator()(Event& event, uint64_t clock, uint64_t iterations) const
            {
                return fn(ptr, arg, event, clock, iterations);
            }

            void* ptr;
            uintptr_t arg;
            fn_ptr fn;

        private:
            template<typename T, typename Fn>
                requires(requires(Fn f, Event e) {
                    { f(nullptr, 0, e, 0) } -> std::same_as<void>;
                })
            void set_fn()
            {
                fn = [](void* ptr, uintptr_t arg, Event& event, uint64_t clock, uint64_t)
                    -> uint64_t {
                    Fn{}((T*)ptr, arg, event, clock);
                    return 0;
                };
            }

            template<typename T, typename Fn>
                requires(requires(Fn f, Event e) {
                    { f(nullptr, 0, e, 0, 0) } -> std::integral;
                })
            void set_fn()
            {
                fn = [](void* ptr, uintptr_t arg, Event& event, uint64_t clock, uint64_t its)
                    -> uint64_t { return Fn{}((T*)ptr, arg, event, clock, its); };
            }
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
