#include "iop.h"
#include "util.h"

#include "libupse/upse-ps1-memory-manager.h"

#include <fmt/format.h>

#include <functional>
#include <utility>
#include <type_traits>

std::unordered_map<iop_table_key, PSF2::iop_builtin_handler> PSF2::builtin_iop_fns = {
    {{"stdio", 4}, &PSF2::iop_printf},

    {{"ioman", 4}, &PSF2::iop_builtin<&PSF2::iop_open, int(const char* name, int mode)>},
    {{"ioman", 5}, &PSF2::iop_builtin<&PSF2::iop_close, int(int fd)>},
    {{"ioman", 6}, &PSF2::iop_builtin<&PSF2::iop_read, int(int fd, void* ptr, size_t size)>},
    {{"ioman", 8}, &PSF2::iop_builtin<&PSF2::iop_lseek, int(int fd, int pos, int mode)>},
    {{"ioman", 20}, &PSF2::iop_AddDrv},
    {{"ioman", 21}, &PSF2::iop_DelDrv},

    {{"sysmem", 4}, &PSF2::iop_builtin<&PSF2::iop_AllocSysMemory, void*(int, int, void*)>},
    {{"sysmem", 5}, &PSF2::iop_builtin<&PSF2::iop_FreeSysMemory, int(void* ptr)>},

    {{"modload", 7},
     &PSF2::iop_builtin<&PSF2::iop_LoadStartModule, int(char const*, int, char const*, int*)>},

    {{"intrman", 4}, &PSF2::iop_RegisterIntrHandler},
    {{"intrman", 5}, &PSF2::iop_ReleaseIntrHandler},
    {{"intrman", 6}, &PSF2::iop_EnableIntr},
    {{"intrman", 7}, &PSF2::iop_DisableIntr},
    {{"intrman", 17}, &PSF2::iop_CpuSuspendIntr},
    {{"intrman", 18}, &PSF2::iop_CpuResumeIntr},

    {{"loadcore", 6}, &PSF2::iop_RegisterLibraryEntries},

    {{"sysclib", 14}, &PSF2::iop_builtin<&PSF2::iop_memset, void*(void* ptr, int c, size_t n)>},
    {{"sysclib", 17}, &PSF2::iop_builtin<&PSF2::iop_bzero, void(void* ptr, size_t n)>},
    {{"sysclib", 23}, &PSF2::iop_builtin<&PSF2::iop_strcpy, char*(char* dst, char const* src)>},
    {{"sysclib", 27}, &PSF2::iop_builtin<&PSF2::iop_strlen, size_t(char const* str)>},
    {{"sysclib", 30},
     &PSF2::iop_builtin<&PSF2::iop_strncpy, char*(char* dst, char const* src, size_t size)>},
    {{"sysclib", 36},
     &PSF2::iop_builtin<&PSF2::iop_strtol, long(char const* nptr, char** endptr, int base)>},
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

template<auto F, typename Signature>
struct iop_builtin_impl;

template<auto F, typename Result, typename... Args>
struct iop_builtin_impl<F, Result(Args...)> {

    static uint32_t get_raw_arg(upse_module_instance_t* ins, size_t index)
    {
        if (index <= 3) { // Registers $a0 - $a3
            return ins->cpustate.GPR.r[index + 4];
        }
        // Parameters from the stack.
        auto const sp = from_le(ins->cpustate.GPR.n.sp);
        auto* sp_ptr = (uint32_t*)PSXM(ins, sp);
        if (sp_ptr) {
            return *(sp_ptr + index);
        }
        // TODO: warning message?
        return 0;
    };

    template<typename Arg, size_t Index>
    static auto argument(upse_module_instance_t* ins)
    {
        uint32_t const raw_arg = get_raw_arg(ins, Index);

        if constexpr (std::is_pointer_v<Arg>) {
            auto const ptr = (Arg)PSXM(ins, raw_arg);
            return PSF2::PointerArg{ptr, raw_arg};
        } else {
            return raw_arg;
        }
    }

    template<typename I, I... Ints>
    static std::optional<uint32_t>
        run(PSF2& psf2, upse_module_instance_t* ins, std::index_sequence<Ints...>)
    {
        if constexpr (std::is_void_v<Result>) {
            (psf2.*F)(argument<Args, Ints>(ins)...);
            return std::nullopt;
        } else {
            if constexpr (requires() { (psf2.*F)(argument<Args, Ints>(ins)...); }) {
                return (psf2.*F)(argument<Args, Ints>(ins)...);
            } else {
                // Some implementations require a pointer to the emulator instance.
                return (psf2.*F)(ins, argument<Args, Ints>(ins)...);
            }
        }
    }

    static std::optional<uint32_t> run(PSF2& psf2, upse_module_instance_t* ins)
    {
        return run(psf2, ins, std::make_index_sequence<sizeof...(Args)>{});
    }
};

template<auto F, typename Signature>
std::optional<uint32_t> PSF2::iop_builtin(upse_module_instance_t* ins)
{
    return iop_builtin_impl<F, Signature>::run(*this, ins);
}
