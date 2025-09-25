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

uint32_t PSF2::iop_strcpy(upse_module_instance_t* ins)
{
    auto const dst_u32 = from_le(ins->cpustate.GPR.n.a0);
    auto* const dst = (char*)PSXM(ins, dst_u32);
    auto* const src = (char*)PSXM(ins, from_le(ins->cpustate.GPR.n.a1));
    strcpy(dst, src);
    return dst_u32;
}

uint32_t PSF2::iop_strlen(upse_module_instance_t* ins)
{
    auto* const ptr = (char*)PSXM(ins, from_le(ins->cpustate.GPR.n.a0));
    return strlen(ptr);
}

uint32_t PSF2::iop_strncpy(upse_module_instance_t* ins)
{
    auto const dst_u32 = from_le(ins->cpustate.GPR.n.a0);
    auto* const dst = (char*)PSXM(ins, dst_u32);
    auto* const src = (char*)PSXM(ins, from_le(ins->cpustate.GPR.n.a1));
    uint32_t const dsize = from_le(ins->cpustate.GPR.n.a2);
    strncpy(dst, src, dsize);
    return dst_u32;
}

uint32_t PSF2::iop_strtol(upse_module_instance_t* ins)
{
    auto const nptr_u32 = from_le(ins->cpustate.GPR.n.a0);
    auto* const nptr = (char*)PSXM(ins, nptr_u32);
    uint32_t const endptr = from_le(ins->cpustate.GPR.n.a1);
    int const base = from_le(ins->cpustate.GPR.n.a2);

    char* ptr{};
    auto result = strtol(nptr, &ptr, base);

    if (endptr) {
        if (!ptr) {
            PSXMu32(ins, endptr) = 0;
        } else {
            auto const diff = ptr - nptr;
            PSXMu32(ins, endptr) = from_le(nptr_u32 + diff);
        }
    }

    result = std::min<long>(std::numeric_limits<int32_t>::max(), result);
    result = std::max<long>(std::numeric_limits<int32_t>::min(), result);

    return result;
}
