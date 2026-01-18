// SPDX-FileCopyrightText: 2026 Aitor Esteve Alvarado
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "r3000.h"

#include <memory>
#include <concepts>
#include <cstdint>

class MMAPR3000Bus {
public:
    explicit MMAPR3000Bus();
    ~MMAPR3000Bus();

    void init(R3000* emu);

    template<std::integral Int>
    Int read_mem(uint32_t addr) {
        return read_mem_impl<Int>(_mem_space + addr);
    }

    template<std::integral Int>
    void write_mem(uint32_t addr, Int value) {
        write_mem_impl<Int>(_mem_space + addr, value);
    }

    template<std::integral Int>
    static Int read_mem_impl(void* addr);

    template<std::integral Int>
    static void write_mem_impl(void* addr, Int value);

    uint8_t* get_mem_ptr() {
        return _mem_space;
    }

    uint8_t* get_device_mem_ptr();

private:
    struct Private;
    uint8_t* _mem_space;
    std::unique_ptr<Private> _p;
};
