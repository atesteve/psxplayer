// SPDX-FileCopyrightText: 2026 Aitor Esteve Alvarado
// SPDX-License-Identifier: GPL-3.0-only

#include "iop.h"
#include "util.h"

#include "util/printf-glue.h"
#include "libupse/upse-ps1-memory-manager.h"

#include <cstdio>

namespace {

struct upse_xva_list final : public xva_list {
    explicit upse_xva_list(upse_module_instance_t* ins)
        : ins{ins}
    {}

    upse_module_instance_t* ins;
    int param = 1; // Initialize to 1, second parameter. The first parameter is the format string.

    int next_param() override { return param++; }

    uint32_t* gpr() override { return ins->cpustate.GPR.r; }

    uint32_t* get_u32_ptr(uint32_t addr) override
    {
        auto* const ptr = PSXM(ins, addr);
        return (uint32_t*)ptr;
    }

    char* get_char_ptr(uint32_t addr) override
    {
        auto const addr_in = addr;
        while (true) {
            auto* ptr = (char*)PSXM(ins, addr);
            if (!ptr) {
                return nullptr;
            }
            if (*ptr == 0) {
                return (char*)PSXM(ins, addr_in);
            }
            addr++;
        }
    }

    uint32_t read(uint32_t addr) override { return PSXMu32(ins, addr); }
};

void print(const char* data, int n, void*)
{
    for (int i = 0; i < n; ++i) {
        putchar(data[i]);
    }
}

} // namespace

std::optional<uint32_t> PSF2::iop_printf(upse_module_instance_t* ins)
{
    // $a0 contains a pointer to the format string.
    uint32_t ptr = from_le(ins->cpustate.GPR.n.a0);
    auto* fmt_str = (char const*)PSXM(ins, ptr);

    upse_xva_list va{ins};

    return vxprintf(print, nullptr, fmt_str, va);
}
