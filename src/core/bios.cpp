#include "bios.h"

#include <fmt/format.h>

#include <unordered_map>
#include <variant>
#include <functional>

namespace {

using bios_call_result = std::variant<std::monostate, uint32_t, Bios::noreturn>;

template<auto Fn, typename Signature>
struct bcall_impl;

template<auto Fn, typename Result, typename... Args>
struct bcall_impl<Fn, Result (Bios::*)(Args...)> {

    static uint32_t get_raw_arg(R3000 const& emu, Core const& core, size_t index)
    {
        if (index <= 3) { // Registers $a0 - $a3
            return core.gpr.r[index + 4];
        }
        // Parameters from the stack.
        return emu.read_mem<uint32_t>(core.gpr.n.sp);
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
    static bios_call_result run(Bios& bios, R3000& emu, std::index_sequence<Ints...>)
    {
        using FirstArg = std::tuple_element_t<0, std::tuple<Args...>>;
        constexpr bool sub_one = std::is_same_v<FirstArg, R3000&>;
        auto const& core = emu.core();

        if constexpr (std::is_void_v<Result>) {
            std::invoke(Fn, bios, get_argument<Args, Ints - sub_one>(emu, core)...);
            return {};
        } else if constexpr (std::is_convertible_v<Result, uint32_t>) {
            return (uint32_t)std::invoke(
                Fn, bios, get_argument<Args, Ints - sub_one>(emu, core)...);
        } else {
            return std::invoke(Fn, bios, get_argument<Args, Ints - sub_one>(emu, core)...);
        }
    }

    static bios_call_result run(Bios& bios, R3000&, std::index_sequence<>)
    {
        if constexpr (std::is_void_v<Result>) {
            std::invoke(Fn, bios);
            return {};
        } else {
            return std::invoke(Fn, bios);
        }
    }

    static bios_call_result run(Bios& bios, R3000& emu)
    {
        return run(bios, emu, std::make_index_sequence<sizeof...(Args)>{});
    }
};

template<auto Fn>
bios_call_result bcall(Bios& bios, R3000& emu)
{
    return bcall_impl<Fn, decltype(Fn)>::run(bios, emu);
}

std::unordered_map<uint32_t, bios_call_result (*)(Bios& bios, R3000& emu)> const bios_fns = {
    {0xa013, bcall<&Bios::setjmp>},
    {0xa014, bcall<&Bios::longjmp>},
    {0xa039, bcall<&Bios::InitHeap>},
    {0xa03f, bcall<&Bios::printf>},
    {0xa072, bcall<&Bios::unimplemented>},

    {0xb007, bcall<&Bios::deliverEvent>},
    {0xb008, bcall<&Bios::openEvent>},
    {0xb009, bcall<&Bios::closeEvent>},
    {0xb00a, bcall<&Bios::waitEvent>},
    {0xb00b, bcall<&Bios::testEvent>},
    {0xb00c, bcall<&Bios::enableEvent>},
    {0xb019, bcall<&Bios::HookEntryInt>},
    {0xb017, bcall<&Bios::returnFromException>},
    {0xb05b, bcall<&Bios::unimplemented>},

    {0xc00a, bcall<&Bios::setIrqAutoAck>},
};

} // namespace

void Bios::run_bios_fn(R3000& emu, uint32_t group)
{
    auto& core = emu.core();
    auto const index = core.gpr.n.t1;
    auto const key = (group << 8) | index;
    auto const it = bios_fns.find(key);
    if (it == bios_fns.cend()) {
        fmt::println("Unsupported bios call: {:x} {:x}", group, index);
        throw std::exception{};
    }
    auto const result = it->second(*this, emu);
    if (holds_alternative<uint32_t>(result)) {
        core.gpr.n.v0 = get<uint32_t>(result);
        core.pc = core.gpr.n.ra;
    } else if (holds_alternative<std::monostate>(result)) {
        core.pc = core.gpr.n.ra;
    } else {
        // Do nothing in this case, the implementation already assigned the PC (noreturn function).
    }
}
