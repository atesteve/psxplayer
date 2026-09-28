// SPDX-FileCopyrightText: 2026 Aitor Esteve Alvarado
// SPDX-License-Identifier: GPL-3.0-only

#include "bios.h"

#include "util/printf-glue.h"

namespace {

struct R3000_xva_list final : public xva_list {
    explicit R3000_xva_list(R3000* emu)
        : emu{emu}
        , ram{emu->get_buffer(0, 0x200000)}
    {}

    R3000* emu;
    EmuBuffer<uint8_t> ram;
    int param = 1; // Initialize to 1, second parameter. The first parameter is the format string.

    int next_param() override { return param++; }

    uint32_t* gpr() override { return emu->core().gpr.r.data(); }

    uint32_t* get_u32_ptr(uint32_t addr) override
    {
        auto buf = emu->get_buffer<uint32_t>(addr, 1);
        return buf.data();
    }

    char* get_char_ptr(uint32_t addr) override
    {
        // Bring the pointer to the first RAM mirror.
        addr &= 0x1fffffu;
        auto const addr_in = addr;
        // Read from the given addr, until a null char is found. The reads are checked, so if the
        // string lies out of bounds, it will throw.
        while (ram[addr] != 0) {
            addr++;
        }
        // It is safe to pass the raw pointer.
        return (char*)&ram[addr_in];
    }

    uint32_t read(uint32_t addr) override { return emu->read_mem<uint32_t>(addr); }
};

void print(const char* data, int n, void*)
{
    for (int i = 0; i < n; ++i) {
        putchar(data[i]);
    }
}

} // namespace

int32_t Bios::printf(R3000& emu, r3000_ptr_t fmt)
{
    R3000_xva_list va{&emu};
    char* fmt_ptr = va.get_char_ptr(fmt);
    return vxprintf(print, nullptr, fmt_ptr, va);
}
