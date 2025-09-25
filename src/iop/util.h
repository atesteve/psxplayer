#pragma once

#include <type_traits>
#include <memory>
#include <cstring>
#include <bit>

namespace stdx {

// https://stackoverflow.com/questions/76445860/implementation-of-stdstart-lifetime-as
template<class T>
    requires(std::is_trivially_copyable_v<T>)
T* start_lifetime_as(void* p) noexcept
{
    return std::launder(static_cast<T*>(std::memmove(p, p, sizeof(T))));
}

template<class T>
    requires(std::is_trivially_copyable_v<T>)
T const* start_lifetime_as(const void* p) noexcept
{
    return std::launder(static_cast<T const*>(std::memmove(const_cast<void*>(p), p, sizeof(T))));
}

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
