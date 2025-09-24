#include "iop.h"
#include "util.h"

#include "xprintf/xprintf.h"
#include "libupse/upse-ps1-memory-manager.h"

#include <type_traits>
#include <cstdio>

struct xva_list {
    upse_module_instance_t* ins;
    int param;
};

template<typename T>
T xva_arg_impl(xva_list& ap)
{
    auto const param = ap.param;
    ap.param++;
    if constexpr (std::is_same_v<T, double>) {
        // Just don't implement this.
        return {};
    } else {
        // Get the next parameter from either a register or the stack.
        void* param_ptr = [&] -> uint32_t* {
            if (param <= 3) { // Registers $a0 - $a3
                return &ap.ins->cpustate.GPR.r[param + 4];
            }
            // Parameters from the stack.
            auto const sp = from_le(ap.ins->cpustate.GPR.n.sp);
            auto* sp_ptr = (uint32_t*)PSXM(ap.ins, sp);
            if (sp_ptr) {
                return sp_ptr + param;
            }
            return nullptr;
        }();

        // Can happen if the sp is somehow corrupt.
        if (!param_ptr) {
            return {};
        }

        if constexpr (std::is_pointer_v<T>) {
            // If the parameter is a pointer, we need to return the translated pointer to emulated
            // PSX memory.
            uint32_t ptr = from_le(*(uint32_t*)param_ptr);
            return (T)PSXM(ap.ins, ptr);
        } else {
            using RetType = std::conditional_t<std::is_signed_v<T>, int32_t, uint32_t>;
            return from_le(*(RetType*)param_ptr);
        }
    }
}

// Explicit template instantiations
template int xva_arg_impl(xva_list&);
template long xva_arg_impl(xva_list&);
template unsigned int xva_arg_impl(xva_list&);
template unsigned long xva_arg_impl(xva_list&);
template double xva_arg_impl(xva_list&);
template int* xva_arg_impl(xva_list&);
template char* xva_arg_impl(xva_list&);

namespace {
void print(const char* data, int n, void*)
{
    for (int i = 0; i < n; ++i) {
        putchar(data[i]);
    }
}

} // namespace

uint32_t PSF2::iop_printf(upse_module_instance_t* ins)
{
    // $a0 contains a pointer to the format string.
    uint32_t ptr = from_le(ins->cpustate.GPR.n.a0);
    auto* fmt_str = (char const*)PSXM(ins, ptr);

    xva_list va{
        .ins = ins,
        .param = 1, // Initialize to 1, second parameter. The first parameter is the format string.
    };

    return vxprintf(print, nullptr, fmt_str, va);
}
