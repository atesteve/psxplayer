#pragma once

#include "r3000.h"

#include <cstdint>

class Bios {
public:
    void run_bios_fn(R3000& emu, uint32_t group);

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
