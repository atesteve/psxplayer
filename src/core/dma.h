// SPDX-FileCopyrightText: 2026 Aitor Esteve Alvarado
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "r3000.h"

#include <cstdint>
#include <memory>

class DMA {
public:
    explicit DMA();
    ~DMA();

    void init(R3000* emu);

    void write_reg(r3000_ptr_t addr, uint32_t value);
    uint32_t read_reg(r3000_ptr_t addr);

    void request_transfer(uint32_t channel, bool request);
    bool get_master_irq_flag();
private:
    struct Private;
    std::unique_ptr<Private> _p;
};
