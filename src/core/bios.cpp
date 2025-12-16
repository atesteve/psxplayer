#include "bios.h"

#include <fmt/format.h>

#include <unordered_map>
#include <optional>

namespace {

template<auto Fn, typename Signature>
struct iop_builtin_impl;

template<auto Fn, typename Result, typename... Args>
struct iop_builtin_impl<Fn, Result (*)(Args...)> {

    static uint32_t get_raw_arg(R3000 const& emu, size_t index)
    {
        auto const& core = emu.core();
        if (index <= 3) { // Registers $a0 - $a3
            return core.gpr[index + 4];
        }
        // Parameters from the stack.
        return emu.read_mem<uint32_t>(core.gpr[GPRName::sp]);
    };

    template<size_t... Ints>
    static std::optional<uint32_t>
        run(R3000 const& emu, std::index_sequence<Ints...>)
    {
        if constexpr (std::is_void_v<Result>) {
            Fn(get_raw_arg<Args, Ints>(emu)...);
            return std::nullopt;
        } else {
            return Fn(get_argument<Args, Ints>(emu)...);
        }
    }

    static std::optional<uint32_t> run(R3000 const& emu)
    {
        return run(emu, std::make_index_sequence<sizeof...(Args)>{});
    }
};

template<auto Fn>
std::optional<uint32_t> bcall(R3000& emu)
{
    return iop_builtin_impl<Fn, decltype(Fn)>::run(emu);
}

std::unordered_map<uint32_t, std::optional<uint32_t>(*)(R3000& emu)> const bios_fns = {

};

}

void Bios::run_bios_fn(R3000& emu, uint32_t group)
{
    auto& core = emu.core();
    auto const index = core.gpr[GPRName::t1];
    auto const key = (group << 8) | index;
    auto const it = bios_fns.find(key);
    if (it == bios_fns.cend()) {
        fmt::println("Unsupported bios call: {:x} {:x}", group, index);
        throw std::exception{};
    }
    auto const result = it->second(emu);
    if (result) {
        core.gpr[GPRName::v0] = *result;
    }
}
