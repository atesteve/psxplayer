#include "bios.h"

#include <fmt/format.h>

#include <unordered_map>
#include <optional>
#include <functional>

namespace {

template<auto Fn, typename Signature>
struct bcall_impl;

template<auto Fn, typename Result, typename... Args>
struct bcall_impl<Fn, Result (Bios::*)(Args...)> {

    static uint32_t get_raw_arg(R3000 const& emu, Core const& core, size_t index)
    {
        if (index <= 3) { // Registers $a0 - $a3
            return core.gpr[index + 4];
        }
        // Parameters from the stack.
        return emu.read_mem<uint32_t>(core.gpr[GPRName::sp]);
    }

    template<typename Arg, size_t Index>
    static decltype(auto) get_argument(R3000& emu, Core const& core)
    {
        if constexpr (std::is_same_v<Arg, R3000&>) {
            return emu;
        } else if constexpr (requires(Arg a) { a.size_bytes(); }) {
            uint32_t const raw_arg = get_raw_arg(emu, core, Index);
            return emu.get_buffer<typename Arg::type>(raw_arg, 1);
        } else {
            return get_raw_arg(emu, core, Index);
        }
    }

    template<size_t... Ints>
    static std::optional<uint32_t> run(Bios& bios, R3000& emu, std::index_sequence<Ints...>)
    {
        using FirstArg = std::tuple_element_t<0, std::tuple<Args...>>;
        constexpr bool sub_one = std::is_same_v<FirstArg, R3000&>;
        auto const& core = emu.core();

        if constexpr (std::is_void_v<Result>) {
            std::invoke(Fn, bios, get_argument<Args, Ints - sub_one>(emu, core)...);
            return std::nullopt;
        } else {
            return std::invoke(Fn, bios, get_argument<Args, Ints - sub_one>(emu, core)...);
        }
    }

    static std::optional<uint32_t> run(Bios& bios, R3000&, std::index_sequence<>)
    {
        if constexpr (std::is_void_v<Result>) {
            std::invoke(Fn, bios);
            return std::nullopt;
        } else {
            return std::invoke(Fn, bios);
        }
    }

    static std::optional<uint32_t> run(Bios& bios, R3000& emu)
    {
        return run(bios, emu, std::make_index_sequence<sizeof...(Args)>{});
    }
};

template<auto Fn>
std::optional<uint32_t> bcall(Bios& bios, R3000& emu)
{
    return bcall_impl<Fn, decltype(Fn)>::run(bios, emu);
}

std::unordered_map<uint32_t, std::optional<uint32_t> (*)(Bios& bios, R3000& emu)> const bios_fns = {
    {0xa013, bcall<&Bios::setjmp>},
    {0xa014, bcall<&Bios::longjmp>},
    {0xa039, bcall<&Bios::InitHeap>},
    {0xa03f, bcall<&Bios::printf>},
    {0xa072, bcall<&Bios::unimplemented>},

    {0xb008, bcall<&Bios::openEvent>},
    {0xb008, bcall<&Bios::closeEvent>},
    {0xb00a, bcall<&Bios::waitEvent>},
    {0xb00b, bcall<&Bios::testEvent>},
    {0xb00c, bcall<&Bios::enableEvent>},
    {0xb019, bcall<&Bios::HookEntryInt>},
    {0xb05b, bcall<&Bios::unimplemented>},

    {0xc00a, bcall<&Bios::setIrqAutoAck>},
};

} // namespace

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
    auto const result = it->second(*this, emu);
    if (result) {
        core.gpr[GPRName::v0] = *result;
    }
}
