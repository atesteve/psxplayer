// SPDX-FileCopyrightText: 2026 Aitor Esteve Alvarado
// SPDX-License-Identifier: GPL-3.0-only

#include "iop.h"
#include "util.h"

#include "libupse/upse-ps1-memory-manager.h"

#include <cstring>
#include <limits>

using namespace std::literals;

uint32_t PSF2::iop_memset(PointerArg<void*> ptr, int c, uint32_t n)
{
    memset(ptr.ptr, c, n);
    return ptr.raw_ptr;
}

void PSF2::iop_bzero(PointerArg<void*> ptr, uint32_t n)
{
    memset(ptr.ptr, 0, n);
}

uint32_t PSF2::iop_strcpy(PointerArg<char*> dst, PointerArg<char const*> src)
{
    strcpy(dst.ptr, src.ptr);
    return dst.raw_ptr;
}

uint32_t PSF2::iop_strlen(PointerArg<char const*> str)
{
    return strlen(str.ptr);
}

uint32_t PSF2::iop_strncpy(PointerArg<char*> dst, PointerArg<char const*> src, uint32_t size)
{
    strncpy(dst.ptr, src.ptr, size);
    return dst.raw_ptr;
}

int32_t PSF2::iop_strtol(upse_module_instance_t* ins,
                         PointerArg<char const*> nptr,
                         PointerArg<char**> endptr,
                         int base)
{
    char* ptr{};
    auto result = strtol(nptr.ptr, &ptr, base);

    if (endptr.raw_ptr) {
        if (!ptr) {
            PSXMu32(ins, endptr.raw_ptr) = 0;
        } else {
            auto const diff = ptr - nptr.ptr;
            PSXMu32(ins, endptr.raw_ptr) = from_le(nptr.raw_ptr + diff);
        }
    }

    result = std::min<long>(std::numeric_limits<int32_t>::max(), result);
    result = std::max<long>(std::numeric_limits<int32_t>::min(), result);

    return result;
}
