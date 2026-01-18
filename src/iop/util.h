// SPDX-FileCopyrightText: 2026 Aitor Esteve Alvarado
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <type_traits>
#include <memory>
#include <cstring>
#include <bit>
#include <string>
#include <string_view>

namespace stdx {

#ifdef __cpp_lib_start_lifetime_as

template<typename T>
T* start_lifetime_as(void* p) noexcept
{
    return std::start_lifetime_as<T>(p);
}

template<typename T>
T const* start_lifetime_as(void const* p) noexcept
{
    return std::start_lifetime_as<T>(p);
}

#else

// https://stackoverflow.com/questions/76445860/implementation-of-stdstart-lifetime-as
template<typename T>
    requires(std::is_trivially_copyable_v<T>)
T* start_lifetime_as(void* p) noexcept
{
    return std::launder(static_cast<T*>(std::memmove(p, p, sizeof(T))));
}

template<typename T>
    requires(std::is_trivially_copyable_v<T>)
T const* start_lifetime_as(void const* p) noexcept
{
    return std::launder(static_cast<T const*>(std::memmove(const_cast<void*>(p), p, sizeof(T))));
}

#endif

} // namespace stdx

template<typename... Callable>
struct visitor : Callable... {
    using Callable::operator()...;
};

template<std::integral Int>
Int from_le(Int i)
{
    if constexpr (std::endian::native == std::endian::big) {
        return std::byteswap(i);
    } else {
        return i;
    }
}

template<std::integral Int>
Int to_le(Int i)
{
    if constexpr (std::endian::native == std::endian::big) {
        return std::byteswap(i);
    } else {
        return i;
    }
}

template<std::integral Int>
Int load(uint8_t const* buf)
{
    Int ret{};

    ret = (static_cast<Int>(buf[0]) & 0xff);
    if constexpr (sizeof(Int) > 1) {
        ret |= (static_cast<Int>(buf[1]) & 0xff) << 8;
    }
    if constexpr (sizeof(Int) > 2) {
        ret |= (static_cast<Int>(buf[2]) & 0xff) << 16;
        ret |= (static_cast<Int>(buf[3]) & 0xff) << 24;
    }

    return from_le(ret);
}

std::string_view load_string(auto const& buf, size_t max_size)
{
    std::string_view str{(char*)&buf[0], max_size};
    return str.substr(0, str.find_first_of('\0'));
}

template<size_t N>
std::string_view load_string(char const (&buf)[N])
{
    std::string_view str{buf, N};
    return str.substr(0, str.find_first_of('\0'));
}

#ifdef _MSC_VER

inline std::string operator+(std::string const& s1, std::string_view s2) {
    return s1 + std::string{s2};
}

inline std::string operator+(std::string_view s1, std::string const& s2) {
    return std::string{s1} + s2;
}

#endif
