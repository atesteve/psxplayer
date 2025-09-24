#include "iop.h"
#include "util.h"

#include "libupse/upse-ps1-memory-manager.h"

#include <string>

using namespace std::literals;

uint32_t PSF2::iop_LoadStartModule(upse_module_instance_t* ins)
{
    auto* const c_name = (char const*)PSXM(ins, from_le(ins->cpustate.GPR.n.a0));
    int const arglen = from_le(ins->cpustate.GPR.n.a1);
    uint32_t const args = from_le(ins->cpustate.GPR.n.a2);

    if (!c_name) {
        return -1;
    }

    auto const name = load_string("/"s + c_name, 256);
    auto const entry_point = load_irx(ins, name);

    // Save state.
    loadStartModule_saved_state =  {
        ins->cpustate.GPR.n.ra,
        ins->cpustate.GPR.n.a0,
        ins->cpustate.GPR.n.a1,
        ins->cpustate.GPR.n.a2,
        ins->cpustate.GPR.n.a3,
    };
    // Set ra to a hook to recover control after the module call.
    ins->cpustate.GPR.n.ra = to_le(0x80000008);
    // Set the emulator jump address to the module's entry point.
    ins->cpustate.branchPC = to_le(entry_point);

    // Set parameters.
    ins->cpustate.GPR.n.a0 = to_le(arglen);
    ins->cpustate.GPR.n.a1 = to_le(args);

    return 0;
}

uint32_t PSF2::iop_LoadStartModuleReturn(upse_module_instance_t* ins)
{
    // Restore state
    ins->cpustate.GPR.n.ra = loadStartModule_saved_state.ra;
    ins->cpustate.GPR.n.a0 = loadStartModule_saved_state.a0;
    ins->cpustate.GPR.n.a1 = loadStartModule_saved_state.a1;
    ins->cpustate.GPR.n.a2 = loadStartModule_saved_state.a1;
    ins->cpustate.GPR.n.a3 = loadStartModule_saved_state.a3;

    // Set return value.
    uint32_t const result = from_le(ins->cpustate.GPR.n.a3);
    PSXMu32(ins, result) = ins->cpustate.GPR.n.v0;

    // Set the emulator jump address to the original return address.
    ins->cpustate.branchPC = ins->cpustate.GPR.n.ra;

    return 0;
}
