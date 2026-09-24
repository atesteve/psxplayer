// SPDX-FileCopyrightText: 2026 Aitor Esteve Alvarado
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "core/r3000.h"

#include <cstdint>
#include <memory>
#include <span>

class SPU {
public:
    explicit SPU();
    ~SPU();

    void init(R3000* emu);

    void write_reg(r3000_ptr_t addr, uint16_t value);
    uint16_t read_reg(r3000_ptr_t addr);

    uint64_t dma_write(r3000_ptr_t addr, uint32_t nbytes);
    uint64_t dma_read(r3000_ptr_t addr, uint32_t nbytes);

    void set_output_buffer(std::span<int16_t> output);
    size_t rendered_samples() const;

private:
    struct Private;
    std::unique_ptr<Private> _p;
};
