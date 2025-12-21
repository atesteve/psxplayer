#include "bios.h"

#include "xprintf/xprintf.h"

uint32_t Bios::setjmp(R3000& emu, EmuBuffer<psx_jmp_buf> buf)
{
    auto const& regs = emu.core().gpr;
    *buf = {
        .ra = regs[GPRName::ra],
        .sp = regs[GPRName::sp],
        .s8 = regs[GPRName::s8],
        .s0 = regs[GPRName::s0],
        .s1 = regs[GPRName::s1],
        .s2 = regs[GPRName::s2],
        .s3 = regs[GPRName::s3],
        .s4 = regs[GPRName::s4],
        .s5 = regs[GPRName::s5],
        .s6 = regs[GPRName::s6],
        .s7 = regs[GPRName::s7],
        .gp = regs[GPRName::gp],
    };
    return 1;
}

uint32_t Bios::longjmp(R3000& emu, EmuBuffer<psx_jmp_buf> buf, uint32_t ret)
{
    auto& regs = emu.core().gpr;
    regs[GPRName::ra] = buf->ra;
    regs[GPRName::sp] = buf->sp;
    regs[GPRName::s8] = buf->s8;
    regs[GPRName::s0] = buf->s0;
    regs[GPRName::s1] = buf->s1;
    regs[GPRName::s2] = buf->s2;
    regs[GPRName::s3] = buf->s3;
    regs[GPRName::s4] = buf->s4;
    regs[GPRName::s5] = buf->s5;
    regs[GPRName::s6] = buf->s6;
    regs[GPRName::s7] = buf->s7;
    regs[GPRName::gp] = buf->gp;
    return ret;
}
