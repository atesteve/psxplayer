#include "iop.h"
#include "util.h"

#include "libupse/upse-ps1-memory-manager.h"

#include <cstring>

using namespace std::literals;

uint32_t PSF2::iop_memset(upse_module_instance_t* ins)
{
    auto* const ptr = (char*)PSXM(ins, from_le(ins->cpustate.GPR.n.a0));
    int const c = from_le(ins->cpustate.GPR.n.a1);
    uint32_t const n = from_le(ins->cpustate.GPR.n.a2);

    memset(ptr, c, n);

    return from_le(ins->cpustate.GPR.n.a0);
}

uint32_t PSF2::iop_strlen(upse_module_instance_t* ins)
{
    auto* const ptr = (char*)PSXM(ins, from_le(ins->cpustate.GPR.n.a0));
    return strlen(ptr);
}

uint32_t PSF2::iop_strncpy(upse_module_instance_t* ins)
{
    auto const dst_arg = from_le(ins->cpustate.GPR.n.a0);
    auto* const dst = (char*)PSXM(ins, dst_arg);
    auto* const src = (char*)PSXM(ins, from_le(ins->cpustate.GPR.n.a1));
    uint32_t const dsize = from_le(ins->cpustate.GPR.n.a2);
    strncpy(dst, src, dsize);
    return dst_arg;
}

uint32_t PSF2::iop_strtol(upse_module_instance_t* ins)
{
    auto* const nptr = (char*)PSXM(ins, from_le(ins->cpustate.GPR.n.a0));
    uint32_t const endptr = from_le(ins->cpustate.GPR.n.a1);
    int const base = from_le(ins->cpustate.GPR.n.a2);

    return strtol(nptr, 0, base);
}
