#include "iop.h"
#include "util.h"

#include <utility>

uint32_t PSF2::iop_AllocSysMemory(upse_module_instance_t* ins)
{
    int const mode = from_le(ins->cpustate.GPR.n.a0);
    int const size = from_le(ins->cpustate.GPR.n.a1);
    uint32_t const ptr = from_le(ins->cpustate.GPR.n.a2);

    // Ignore mode and ptr
    std::ignore = mode;
    std::ignore = ptr;

    // Simple bump allocator
    auto const ret = base_addr;
    base_addr += size;
    round_base_addr();

    return ret;
}

uint32_t PSF2::iop_FreeSysMemory(upse_module_instance_t* ins)
{
    std::ignore = ins;
    // Do nothing.
    return 0;
}