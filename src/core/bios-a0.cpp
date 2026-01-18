// SPDX-FileCopyrightText: 2026 Aitor Esteve Alvarado
// SPDX-License-Identifier: GPL-3.0-only

#include "bios.h"

uint32_t Bios::setjmp(R3000& emu, EmuBuffer<psx_jmp_buf> buf)
{
    auto const& regs = emu.core().gpr;
    *buf = {
        .ra = regs.n.ra,
        .sp = regs.n.sp,
        .s8 = regs.n.s8,
        .s0 = regs.n.s0,
        .s1 = regs.n.s1,
        .s2 = regs.n.s2,
        .s3 = regs.n.s3,
        .s4 = regs.n.s4,
        .s5 = regs.n.s5,
        .s6 = regs.n.s6,
        .s7 = regs.n.s7,
        .gp = regs.n.gp,
    };
    return 0;
}

Bios::noreturn Bios::longjmp(R3000& emu, EmuBuffer<psx_jmp_buf> buf, uint32_t ret)
{
    auto& core = emu.core();
    core.gpr.n.ra = buf->ra;
    core.gpr.n.sp = buf->sp;
    core.gpr.n.s8 = buf->s8;
    core.gpr.n.s0 = buf->s0;
    core.gpr.n.s1 = buf->s1;
    core.gpr.n.s2 = buf->s2;
    core.gpr.n.s3 = buf->s3;
    core.gpr.n.s4 = buf->s4;
    core.gpr.n.s5 = buf->s5;
    core.gpr.n.s6 = buf->s6;
    core.gpr.n.s7 = buf->s7;
    core.gpr.n.gp = buf->gp;
    core.gpr.n.v0 = ret;
    core.pc = buf->ra;
    return {};
}
