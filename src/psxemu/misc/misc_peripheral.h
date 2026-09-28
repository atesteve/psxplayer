// SPDX-FileCopyrightText: 2026 Aitor Esteve Alvarado
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "psxemu/core/r3000.h"

#include <cstdint>

class Misc {
public:
    void write_reg(r3000_ptr_t addr, uint32_t value);
    uint32_t read_reg(r3000_ptr_t addr);
};
