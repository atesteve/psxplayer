#include "iop.h"
#include "util.h"

#include <fmt/format.h>

#include <functional>

std::unordered_map<iop_table_key, PSF2::iop_builtin_handler> PSF2::builtin_iop_fns = {
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

    {{"intrman", 5}, &PSF2::iop_ReleaseIntrHandler},
    {{"intrman", 7}, &PSF2::iop_DisableIntr},
    {{"intrman", 17}, &PSF2::iop_CpuSuspendIntr},
    {{"intrman", 18}, &PSF2::iop_CpuResumeIntr},

    {{"loadcore", 6}, &PSF2::iop_RegisterLibraryEntries},

    {{"sysclib", 14}, &PSF2::iop_memset},
    {{"sysclib", 17}, &PSF2::iop_bzero},
    {{"sysclib", 23}, &PSF2::iop_strcpy},
    {{"sysclib", 27}, &PSF2::iop_strlen},
    {{"sysclib", 30}, &PSF2::iop_strncpy},
    {{"sysclib", 36}, &PSF2::iop_strtol},
};

void PSF2::iop_call(upse_module_instance_t* ins)
{
    auto const it = imported_functions.find(ins->cpustate.pc - 8);
    if (it == imported_functions.cend()) {
        // fmt::println("Warning: can't find IOP call at {:#010x}", ins->cpustate.pc - 8);
        return;
    }

    auto& fn = it->second;
    std::visit(visitor{
                   [&](std::nullptr_t) {
                       auto const it = exported_iop_fns.find({fn.name, fn.index});
                       if (it == exported_iop_fns.cend()) {
                           fmt::println(
                               "Warning: unimplemented function: {}, {}", fn.name, fn.index);
                           ins->cpustate.GPR.n.v0 = to_le(-1);
                       } else {
                           auto const address = it->second;
                           fn.handler = address;
                           //    fmt::println("---- Calling native {} {}", fn.name, fn.index);
                           ins->cpustate.branchPC = to_le(address);
                       }
                   },
                   [&](iop_builtin_handler handler) {
                       //    fmt::println("---- Calling builtin {} {}", fn.name, fn.index);
                       auto const result = std::invoke(handler, this, ins);
                       if (result) {
                           ins->cpustate.GPR.n.v0 = to_le(*result);
                       }
                   },
                   [&](uint32_t address) {
                       //    fmt::println("---- Calling native {} {}", fn.name, fn.index);
                       ins->cpustate.branchPC = to_le(address);
                   },
               },
               fn.handler);
}
