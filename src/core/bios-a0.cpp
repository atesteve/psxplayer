#include "bios.h"

uint32_t Bios::setjmp(R3000& emu, EmuBuffer<psx_jmp_buf> buf) {
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
    return 0;
}
