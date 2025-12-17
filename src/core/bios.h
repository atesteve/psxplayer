#pragma once

#include "r3000.h"

#include <cstdint>

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
    void run_bios_fn(R3000& emu, uint32_t group);

    uint32_t setjmp(R3000& emu, EmuBuffer<psx_jmp_buf> buf);
    void InitHeap(R3000& emu, uint32_t base, uint32_t size);

    void unimplemented() {}

    void HookEntryInt(uint32_t entry_point);

    struct State {
        uint32_t int_entry_point{};
    };

    State state;
};
