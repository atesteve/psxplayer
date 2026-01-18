// SPDX-FileCopyrightText: 2026 Aitor Esteve Alvarado
// SPDX-License-Identifier: GPL-3.0-only

#include "printf-glue.h"

#include <type_traits>
#include <cstdlib>

static constexpr size_t GPR_A0 = 4;
static constexpr size_t GPR_SP = 29;

template<typename T>
T xva_arg_impl(xva_list& ap)
{
    auto const param = ap.next_param();

    if constexpr (std::is_same_v<T, double>) {
        // Just don't implement this.
        return {};
    } else {
        auto* const gpr = ap.gpr();
        // Get the next parameter from either a register or the stack.
        auto* const param_ptr = [&] -> uint32_t* {
            if (param <= 3) { // Registers $a0 - $a3
                return &gpr[param + GPR_A0];
            }
            // Parameters from the stack.
            auto const sp = gpr[GPR_SP];
            auto* sp_ptr = ap.get_u32_ptr(sp);
            if (sp_ptr) {
                return sp_ptr + param;
            }
            return nullptr;
        }();

        // Can happen if the sp is somehow corrupt.
        if (!param_ptr) {
            return {};
        }

        // If the parameter is a pointer, there are two possible scenarios: pointer to in32_t, or
        // pointer to char*. The latter is a pointer to a C string, that's why the distinction is
        // important: we need to check that the string doesn't overflow to ummapped memory before
        // passing the raw pointer to vxprintf.
        if constexpr (std::is_same_v<int32_t*, T>) {
            return (int32_t*)ap.get_u32_ptr(*param_ptr);
        } else if constexpr (std::is_same_v<char*, T>) {
            return ap.get_char_ptr(*param_ptr);
        } else {
            return *param_ptr;
        }
    }
}

// Explicit template instantiations
template int xva_arg_impl(xva_list&);
template long xva_arg_impl(xva_list&);
template unsigned int xva_arg_impl(xva_list&);
template unsigned long xva_arg_impl(xva_list&);
template double xva_arg_impl(xva_list&);
template int32_t* xva_arg_impl(xva_list&);
template char* xva_arg_impl(xva_list&);
