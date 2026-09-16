// SPDX-FileCopyrightText: 2026 Aitor Esteve Alvarado
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "r3000.h"

#include <cstdint>
#include <flat_map>

struct psx_jmp_buf {
    uint32_t ra;
    uint32_t sp;
    uint32_t s8;
    uint32_t s0;
    uint32_t s1;
    uint32_t s2;
    uint32_t s3;
    uint32_t s4;
    uint32_t s5;
    uint32_t s6;
    uint32_t s7;
    uint32_t gp;
};

class Bios {
public:
    struct noreturn {};

    void run_bios_fn(R3000& emu, uint32_t group);

    uint32_t setjmp(R3000& emu, EmuBuffer<psx_jmp_buf> buf);
    noreturn longjmp(R3000& emu, EmuBuffer<psx_jmp_buf> buf, uint32_t ret);
    int32_t printf(R3000& emu, r3000_ptr_t fmt);

    void InitHeap(R3000& emu, uint32_t base, uint32_t size);

    void setIrqAutoAck(uint32_t irq, int value);

    void unimplemented() {}

    void deliverEvent(R3000& emu, uint32_t clazz, uint32_t spec);
    uint32_t openEvent(uint32_t clazz, uint32_t spec, uint32_t mode, uint32_t handler);
    int32_t closeEvent(uint32_t event);
    int32_t waitEvent(uint32_t event);
    int32_t testEvent(uint32_t event);
    int32_t enableEvent(uint32_t event);
    int32_t disableEvent(uint32_t event);
    void HookEntryInt(EmuBuffer<psx_jmp_buf> buf);
    noreturn returnFromException(R3000& emu);

    void exception_handler(R3000& emu);
    void init_exception_handlers();

    uint32_t syscall_verifier(R3000& emu);
    uint32_t timer_verifier(R3000& emu, int timer);
    void timer_handler(R3000& emu, int timer);
    uint32_t irq_verifier(R3000& emu);

    struct Event {
        uint32_t clazz;
        uint32_t spec;
        uint32_t mode;
        r3000_ptr_t handler;
        uint32_t flags;
    };

    struct IRQHandler {
        std::function<int32_t(R3000& emu)> verifier{};
        std::function<void(R3000& emu, int32_t)> handler{};
    };

    struct State {
        EmuBuffer<psx_jmp_buf> unhandled_irq_farjmp{};
        int irq_auto_ack[11]{};
        std::flat_map<uint32_t, Event> events;
        uint32_t next_event_id = 0xf1000000;
        struct {
            Core core{};
            sr_t sr{};
        } saved_state{};
        std::array<std::vector<IRQHandler>, 4> irq_handlers;
    };

    State state;
};
