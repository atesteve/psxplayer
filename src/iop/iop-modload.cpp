#include "iop.h"
#include "util.h"

#include "libupse/upse-ps1-memory-manager.h"

#include <ranges>

using namespace std::literals;

std::optional<uint32_t> PSF2::iop_LoadStartModule(upse_module_instance_t* ins)
{
    auto const name_arg = from_le(ins->cpustate.GPR.n.a0);
    auto* const c_name = (char const*)PSXM(ins, name_arg);
    int const arglen = from_le(ins->cpustate.GPR.n.a1);
    uint32_t const args = from_le(ins->cpustate.GPR.n.a2);

    if (!c_name) {
        return -1;
    }

    auto const name = "/"s + load_string(c_name, 256);
    auto const entry_point = load_irx(ins, name);

    // Save state.
    loadStartModule_saved_state = {
        ins->cpustate.GPR.n.ra,
        ins->cpustate.GPR.n.a0,
        ins->cpustate.GPR.n.a1,
        ins->cpustate.GPR.n.a2,
        ins->cpustate.GPR.n.a3,
        ins->cpustate.GPR.n.sp,
    };
    // Set ra to a hook to recover control after the module call.
    ins->cpustate.GPR.n.ra = to_le(0x80000008);
    // Set the emulator jump address to the module's entry point.
    ins->cpustate.branchPC = to_le(entry_point);

    // Setup arguments.

    std::vector<uint32_t> arg_list{};

    uint32_t new_arg = args;
    for (int i = 0; i < arglen; ++i) {
        if (PSXMu8(ins, from_le(args + i)) == 0) {
            arg_list.push_back(new_arg);
            new_arg = args + i + 1;
        }
    }

    // Setup argv in the stack.
    auto sp = from_le(ins->cpustate.GPR.n.sp);
    sp -= (arg_list.size() + 3) * sizeof(uint32_t);

    PSXMu32(ins, sp + 8) = to_le(name_arg);
    for (auto const [i, arg] : std::ranges::enumerate_view{arg_list}) {
        PSXMu32(ins, sp + 12 + i * sizeof(uint32_t)) = to_le(arg);
    }

    ins->cpustate.GPR.n.sp = to_le(sp);

    // Set parameters.
    ins->cpustate.GPR.n.a0 = to_le(arg_list.size() + 1); // argc
    ins->cpustate.GPR.n.a1 = to_le(sp + 8);              // argv

    return 0;
}

std::optional<uint32_t> PSF2::iop_LoadStartModuleReturn(upse_module_instance_t* ins)
{
    // Restore state
    ins->cpustate.GPR.n.ra = loadStartModule_saved_state.ra;
    ins->cpustate.GPR.n.a0 = loadStartModule_saved_state.a0;
    ins->cpustate.GPR.n.a1 = loadStartModule_saved_state.a1;
    ins->cpustate.GPR.n.a2 = loadStartModule_saved_state.a1;
    ins->cpustate.GPR.n.a3 = loadStartModule_saved_state.a3;
    ins->cpustate.GPR.n.sp = loadStartModule_saved_state.sp;

    // Set return value.
    uint32_t const result = from_le(ins->cpustate.GPR.n.a3);
    PSXMu32(ins, result) = ins->cpustate.GPR.n.v0;

    // Set the emulator jump address to the original return address.
    ins->cpustate.branchPC = ins->cpustate.GPR.n.ra;

    return 0;
}
