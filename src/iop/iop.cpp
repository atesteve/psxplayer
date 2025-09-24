#include "iop.h"
#include "util.h"

#include <fmt/format.h>

#include <functional>

std::unordered_map<iop_table_key, PSF2::iop_handler> PSF2::builtin_iop_fns = {
    {{"stdio", 4}, &PSF2::iop_printf},

    {{"ioman", 4}, &PSF2::iop_open},
    {{"ioman", 5}, &PSF2::iop_close},
    {{"ioman", 6}, &PSF2::iop_read},
    {{"ioman", 8}, &PSF2::iop_lseek},
    {{"ioman", 20}, &PSF2::iop_AddDrv},
    {{"ioman", 21}, &PSF2::iop_DelDrv},

    {{"sysmem", 4}, &PSF2::iop_AllocSysMemory},
    {{"sysmem", 5}, &PSF2::iop_FreeSysMemory},

    {{"modload", 7}, &PSF2::iop_LoadStartModule},

    {{"intrman", 17}, &PSF2::iop_CpuSuspendIntr},
    {{"intrman", 18}, &PSF2::iop_CpuResumeIntr},

    {{"loadcore", 6}, &PSF2::iop_RegisterLibraryEntries},

    {{"sysclib", 14}, &PSF2::iop_memset},
    {{"sysclib", 27}, &PSF2::iop_strlen},
    {{"sysclib", 30}, &PSF2::iop_strncpy},
    {{"sysclib", 36}, &PSF2::iop_strtol},
};

void PSF2::iop_call(upse_module_instance_t* ins)
{
    auto const it = imported_functions.find(ins->cpustate.pc - 8);
    if (it == imported_functions.cend()) {
        fmt::println("Warning: can't find IOP call at {:#010x}", ins->cpustate.pc - 8);
        return;
    }

    auto const& fn = it->second;
    if (!fn.handler) {
        fmt::println("Warning: unimplemented function: {}, {}", fn.name, fn.index);
        // Return -1
        ins->cpustate.GPR.n.v0 = to_le(-1);
        return;
    }

    fmt::println("---- Calling {} {}", fn.name, fn.index);
    auto const result = std::invoke(*fn.handler, this, ins);

    // Return value in v0
    ins->cpustate.GPR.n.v0 = to_le(result);
}
