// SPDX-FileCopyrightText: 2026 Aitor Esteve Alvarado
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "r3000.h"

#include <concepts>

class RegisterDispatcher {
public:
    void set_mem_space(uint8_t* mem_space) { _mem_space = mem_space; }

    template<std::integral Int>
    void write_reg(r3000_ptr_t addr, Int value) const;

    template<std::integral Int>
    Int read_reg(r3000_ptr_t addr) const;

    void init(R3000* emu);

private:
    template<std::integral Int>
    Int read(r3000_ptr_t addr) const;

    template<std::integral Int>
    void write(r3000_ptr_t addr, Int value) const;

    uint8_t* _mem_space{};
    R3000* emu;
    DMA* dma;
    SPU* spu;
    TimerHandler* timers;
};
