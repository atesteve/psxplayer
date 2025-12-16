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

    struct empty_block {
        struct empty_block_* next;
        size_t size;
    };

    struct allocated_block {
        uintptr_t dummy;
        size_t size;
    };

    static_assert(sizeof(Bios::empty_block) == (2 * sizeof(void*)),
                  "empty_block is of the wrong size");
    static_assert(sizeof(Bios::allocated_block) == (2 * sizeof(void*)),
                  "allocated_block is of the wrong size");

    empty_block* user_heap_head = nullptr;
    empty_block* kern_heap_head = nullptr;
    empty_block marker = {.next = nullptr, .size = 0};
};
