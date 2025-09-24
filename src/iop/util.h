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
