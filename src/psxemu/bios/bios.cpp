// SPDX-FileCopyrightText: 2026 Aitor Esteve Alvarado
// SPDX-License-Identifier: GPL-3.0-only

#include "bios.h"

#include "util/util.h"

#include <fmt/format.h>

#include <unordered_map>
#include <variant>
#include <functional>

namespace {

constexpr r3000_ptr_t B0TableLocation = 0x400;
constexpr r3000_ptr_t C0TableLocation = 0x800;
constexpr uint32_t BiosTableSize = 256 * sizeof(uint32_t);

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

    static bios_call_result run(Bios& bios, R3000& emu)
        requires(sizeof...(Args) != 0)
    {
        constexpr auto [... index] = std::make_index_sequence<sizeof...(Args)>{};
        constexpr bool sub_one = std::is_same_v<Args...[0], R3000&>;
        auto const& core = emu.core();

        if constexpr (std::is_void_v<Result>) {
            std::invoke(Fn, bios, get_argument<Args, index - sub_one>(emu, core)...);
            return {};
        } else if constexpr (std::is_convertible_v<Result, uint32_t>) {
            return (uint32_t)std::invoke(
                Fn, bios, get_argument<Args, index - sub_one>(emu, core)...);
        } else {
            return std::invoke(Fn, bios, get_argument<Args, index - sub_one>(emu, core)...);
        }
    }

    static bios_call_result run(Bios& bios, R3000&)
        requires(sizeof...(Args) == 0)
    {
        if constexpr (std::is_void_v<Result>) {
            std::invoke(Fn, bios);
            return {};
        } else {
            return std::invoke(Fn, bios);
        }
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
    {0xa02a, bcall<&Bios::memcpy>},
    {0xa02b, bcall<&Bios::memset>},
    {0xa039, bcall<&Bios::InitHeap>},
    {0xa03f, bcall<&Bios::printf>},
    {0xa044, bcall<&Bios::noop>},           // FlushCache
    {0xa070, bcall<&Bios::noop_return<0>>}, // InitBackupUnit
    {0xa072, bcall<&Bios::noop>},
    {0xa09f, bcall<&Bios::noop>}, // SetMemSize

    {0xb007, bcall<&Bios::deliverEvent>},
    {0xb008, bcall<&Bios::openEvent>},
    {0xb009, bcall<&Bios::closeEvent>},
    {0xb00a, bcall<&Bios::waitEvent>},
    {0xb00b, bcall<&Bios::testEvent>},
    {0xb00c, bcall<&Bios::enableEvent>},
    {0xb00d, bcall<&Bios::disableEvent>},
    {0xb019, bcall<&Bios::HookEntryInt>},
    {0xb017, bcall<&Bios::returnFromException>},
    {0xb04a, bcall<&Bios::noop_return<1>>},               // InitCard
    {0xb04b, bcall<&Bios::noop_return<1>>},               // StartCard
    {0xb056, bcall<&Bios::noop_return<B0TableLocation>>}, // getB0Table
    {0xb057, bcall<&Bios::noop_return<C0TableLocation>>}, // getC0Table
    {0xb05b, bcall<&Bios::noop>},

    {0xc00a, bcall<&Bios::setTimerAutoAck>},
};

} // namespace

void Bios::run_bios_fn(R3000& emu, uint32_t group)
{
    auto& core = emu.core();

    if (group == 0xd0u) {
        emu.return_from_callback();
        return;
    }

    auto const index = core.gpr.n.t1;
    auto const key = (group << 8) | index;
    auto const it = bios_fns.find(key);

    if (it == bios_fns.cend()) {
        fmt::println("Unsupported bios call: {:x} {:x}", group, index);
        throw std::exception{};
    }

    auto const result = it->second(*this, emu);

    result.visit(Visitor{
        [&](uint32_t r) {
            core.gpr.n.v0 = r;
            core.pc = core.gpr.n.ra;
        },
        [&](std::monostate) { core.pc = core.gpr.n.ra; },
        [&](noreturn) { /* Do nothing in this case, the function already assigned the PC. */ },
    });
}

void Bios::init(EmuBuffer<uint8_t> ram)
{
    init_exception_handlers();

    // These are "special" instructions interpretable by the emulator.
    *(uint32_t*)&ram[0xa0] = 0xfc0000a0; // Call A0 BIOS function.
    *(uint32_t*)&ram[0xb0] = 0xfc0000b0; // Call B0 BIOS function.
    *(uint32_t*)&ram[0xc0] = 0xfc0000c0; // Call C0 BIOS function.
    *(uint32_t*)&ram[0xd0] = 0xfc0000d0; // Return to BIOS.

    // Fill the simulated BIOS table region with 0xff for now, if any game tries to use them it will
    // at least crash.
    std::fill_n(&ram[B0TableLocation], BiosTableSize * 2, 0xff);

    // Init timer_auto_ack to 1
    std::ranges::fill(state.timer_auto_ack, 1);
}
