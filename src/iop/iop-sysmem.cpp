// SPDX-FileCopyrightText: 2026 Aitor Esteve Alvarado
// SPDX-License-Identifier: GPL-3.0-only

#include "iop.h"
#include "util.h"

#include <utility>

uint32_t PSF2::iop_AllocSysMemory(int mode, int size, PointerArg<void*> ptr)
{
    // Ignore mode and ptr
    std::ignore = mode;
    std::ignore = ptr;

    // Simple bump allocator
    auto const ret = base_addr;
    base_addr += size;
    round_base_addr();

    return ret;
}

int PSF2::iop_FreeSysMemory(PointerArg<void*> ptr)
{
    std::ignore = ptr;
    // Do nothing.
    return 0;
}