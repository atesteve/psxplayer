// SPDX-FileCopyrightText: 2026 Aitor Esteve Alvarado
// SPDX-License-Identifier: GPL-3.0-only

#include "r3000-impl.h"
#include "mmap-r3000bus.h"
#include "bios.h"
#include "dma.h"
#include "spu/spu.h"
#include "timer/timer.h"
#include "util/util.h"
#include "events.h"

#include <fmt/format.h>
#include <boost/container/static_vector.hpp>

#include <cstdint>
#include <concepts>
#include <bit>
#include <utility>
#include <algorithm>
#include <stdckdint.h>
#include <bit>
#include <stack>
#include <vector>

namespace {

// clang-format off
struct reg_inst_t {
    uint32_t function : 6;
    uint32_t shift    : 5;
    uint32_t rd       : 5;
    uint32_t rt       : 5;
    uint32_t rs       : 5;
    uint32_t opcode   : 6;
};

struct imm_inst_t {
    uint32_t imm    : 16;
    uint32_t rt     : 5;
    uint32_t rs     : 5;
    uint32_t opcode : 6;
};

struct jump_inst_t {
    uint32_t target : 26;
    uint32_t opcode : 6;
};

struct sr_t {
    uint32_t IEc     : 1 = 0;
    uint32_t KUc     : 1 = 0;
    uint32_t IEp     : 1 = 0;
    uint32_t KUp     : 1 = 0;
    uint32_t IEo     : 1 = 0;
    uint32_t KUo     : 1 = 0;
    uint32_t         : 2;
    uint32_t IntMask : 8 = 0;
    uint32_t IsC     : 1 = 0;
    uint32_t SwC     : 1 = 0;
    uint32_t PZ      : 1 = 0;
    uint32_t CM      : 1 = 0;
    uint32_t PE      : 1 = 0;
    uint32_t TS      : 1 = 0;
    uint32_t BEV     : 1 = 0;
    uint32_t         : 2;
    uint32_t RE      : 1 = 0;
    uint32_t         : 2;
    uint32_t CU0     : 1 = 0;
    uint32_t CU1     : 1 = 0;
    uint32_t CU2     : 1 = 0;
    uint32_t CU3     : 1 = 0;
};

struct cause_t {
    uint32_t         : 2;
    uint32_t ExcCode : 5  = 0;
    uint32_t         : 1;
    uint32_t IP      : 8  = 0;
    uint32_t         : 12;
    uint32_t CE      : 2  = 0;
    uint32_t         : 1;
    uint32_t BD      : 1  = 0;
};

// clang-format on

static_assert(sizeof(reg_inst_t) == sizeof(uint32_t));
static_assert(sizeof(imm_inst_t) == sizeof(uint32_t));
static_assert(sizeof(jump_inst_t) == sizeof(uint32_t));
static_assert(sizeof(sr_t) == sizeof(uint32_t));
static_assert(sizeof(cause_t) == sizeof(uint32_t));

uint32_t sign_extend_16(uint32_t x)
{
    return (int16_t)x;
}

} // namespace

template<R3000CoreConfig c>
struct R3000Core<c>::Private {
    explicit Private(R3000Core<c>* parent)
        : parent{parent}
    {
        load_slot.enabled = 0;
    }

    struct HWAlignmentCheck {
        static void enable()
        {
            if constexpr (c.alignment_check == AlignmentCheck::HARDWARE) {
                asm volatile(
                    "subq $128, %%rsp\n"
                    "pushf\n"
                    "orl $0x40000, (%%rsp)\n"
                    "popf\n"
                    "addq $128, %%rsp\n"
                    :);
            }
        }

        static void disable()
        {
            if constexpr (c.alignment_check == AlignmentCheck::HARDWARE) {
                asm volatile(
                    "subq $128, %%rsp\n"
                    "pushf\n"
                    "andl $~0x40000, (%%rsp)\n"
                    "popf\n"
                    "addq $128, %%rsp\n"
                    :);
            }
        }
    };

    void run_instruction();
    uint64_t run_exception(uint32_t code);
    uint64_t run_returnFromException();

    void set_branch_target(r3000_ptr_t target);

    uint64_t run_r_inst(uint32_t function, uint32_t rs, uint32_t rt, uint32_t rd, uint32_t shift);
    uint64_t run_bcond_inst(uint32_t function, uint32_t rs, uint32_t offset);
    uint64_t run_ij_inst(uint32_t opcode, uint32_t rs, uint32_t rt, uint32_t imm, uint32_t target);
    void run_branch(uint32_t offset);

    uint64_t run_r_sll(uint32_t rs, uint32_t rt, uint32_t rd, uint32_t shift);
    uint64_t run_r_srl(uint32_t rs, uint32_t rt, uint32_t rd, uint32_t shift);
    uint64_t run_r_sra(uint32_t rs, uint32_t rt, uint32_t rd, uint32_t shift);
    uint64_t run_r_sllv(uint32_t rs, uint32_t rt, uint32_t rd, uint32_t shift);
    uint64_t run_r_srlv(uint32_t rs, uint32_t rt, uint32_t rd, uint32_t shift);
    uint64_t run_r_srav(uint32_t rs, uint32_t rt, uint32_t rd, uint32_t shift);
    uint64_t run_r_jr(uint32_t rs, uint32_t rt, uint32_t rd, uint32_t shift);
    uint64_t run_r_jalr(uint32_t rs, uint32_t rt, uint32_t rd, uint32_t shift);
    uint64_t run_r_syscall(uint32_t rs, uint32_t rt, uint32_t rd, uint32_t shift);
    uint64_t run_r_break(uint32_t rs, uint32_t rt, uint32_t rd, uint32_t shift);
    uint64_t run_r_mfhi(uint32_t rs, uint32_t rt, uint32_t rd, uint32_t shift);
    uint64_t run_r_mthi(uint32_t rs, uint32_t rt, uint32_t rd, uint32_t shift);
    uint64_t run_r_mflo(uint32_t rs, uint32_t rt, uint32_t rd, uint32_t shift);
    uint64_t run_r_mtlo(uint32_t rs, uint32_t rt, uint32_t rd, uint32_t shift);
    uint64_t run_r_mult(uint32_t rs, uint32_t rt, uint32_t rd, uint32_t shift);
    uint64_t run_r_multu(uint32_t rs, uint32_t rt, uint32_t rd, uint32_t shift);
    uint64_t run_r_div(uint32_t rs, uint32_t rt, uint32_t rd, uint32_t shift);
    uint64_t run_r_divu(uint32_t rs, uint32_t rt, uint32_t rd, uint32_t shift);
    uint64_t run_r_add(uint32_t rs, uint32_t rt, uint32_t rd, uint32_t shift);
    uint64_t run_r_addu(uint32_t rs, uint32_t rt, uint32_t rd, uint32_t shift);
    uint64_t run_r_sub(uint32_t rs, uint32_t rt, uint32_t rd, uint32_t shift);
    uint64_t run_r_subu(uint32_t rs, uint32_t rt, uint32_t rd, uint32_t shift);
    uint64_t run_r_and(uint32_t rs, uint32_t rt, uint32_t rd, uint32_t shift);
    uint64_t run_r_or(uint32_t rs, uint32_t rt, uint32_t rd, uint32_t shift);
    uint64_t run_r_xor(uint32_t rs, uint32_t rt, uint32_t rd, uint32_t shift);
    uint64_t run_r_nor(uint32_t rs, uint32_t rt, uint32_t rd, uint32_t shift);
    uint64_t run_r_slt(uint32_t rs, uint32_t rt, uint32_t rd, uint32_t shift);
    uint64_t run_r_sltu(uint32_t rs, uint32_t rt, uint32_t rd, uint32_t shift);
    uint64_t run_r_unk(uint32_t rs, uint32_t rt, uint32_t rd, uint32_t shift);

    uint64_t run_bcond_bltz(uint32_t rs, uint32_t offset, bool link);
    uint64_t run_bcond_bgez(uint32_t rs, uint32_t offset, bool link);

    uint64_t run_ij_j(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t target);
    uint64_t run_ij_jal(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t target);
    uint64_t run_ij_beq(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t target);
    uint64_t run_ij_bne(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t target);
    uint64_t run_ij_blez(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t target);
    uint64_t run_ij_bgtz(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t target);
    uint64_t run_ij_addi(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t target);
    uint64_t run_ij_addiu(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t target);
    uint64_t run_ij_slti(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t target);
    uint64_t run_ij_sltiu(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t target);
    uint64_t run_ij_andi(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t target);
    uint64_t run_ij_ori(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t target);
    uint64_t run_ij_xori(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t target);
    uint64_t run_ij_lui(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t target);
    uint64_t run_ij_lb(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t target);
    uint64_t run_ij_lh(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t target);
    uint64_t run_ij_lw(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t target);
    uint64_t run_ij_lbu(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t target);
    uint64_t run_ij_lhu(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t target);
    uint64_t run_ij_lwl(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t target);
    uint64_t run_ij_lwr(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t target);
    uint64_t run_ij_sb(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t target);
    uint64_t run_ij_sh(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t target);
    uint64_t run_ij_sw(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t target);
    uint64_t run_ij_swl(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t target);
    uint64_t run_ij_swr(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t target);
    uint64_t run_ij_bios(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t target);
    uint64_t run_ij_unk(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t target);

    template<std::integral Int>
    void check_alignment(r3000_ptr_t addr, AccessType rw) const
    {
        if constexpr (c.alignment_check == AlignmentCheck::SOFTWARE) {
            static constexpr r3000_ptr_t mask = (1 << sizeof(Int)) - 1;
            if (addr & mask) {
                throw AddressException{addr, rw, int_width<Int>};
            }
        }
        // Do nothing otherwise.
    }

    template<std::integral Int>
    Int read_mem(r3000_ptr_t addr)
    {
        check_alignment<Int>(addr, AccessType::READ);
        return bus.read_mem<Int>(addr);
    }

    template<std::integral Int>
    void write_mem(r3000_ptr_t addr, Int value)
    {
        check_alignment<Int>(addr, AccessType::WRITE);
        bus.write_mem<Int>(addr, value);
    }

    struct load_delay_slot {
        uint32_t rt;
        uint32_t value;
        int enabled = 2;
    };

    struct branch_delay_slot {
        int count = 0;
        r3000_ptr_t target{};
    };

    void load_slot_tick()
    {
        if (!load_slot.enabled) {
            return;
        }
        load_slot.enabled--;
        if (!load_slot.enabled) {
            core.gpr.r[load_slot.rt] = load_slot.value;
        }
    }

    void load_slot_flush()
    {
        if (!load_slot.enabled) {
            return;
        }
        load_slot.enabled = 0;
        core.gpr.r[load_slot.rt] = load_slot.value;
    }

    void branch_slot_tick()
    {
        if (branch_slot.count == 0) {
            return;
        }
        branch_slot.count--;
        if (branch_slot.count == 0) {
            core.pc = branch_slot.target;
        }
    }

    void load(uint32_t rt, uint32_t value)
    {
        if (load_slot.enabled) {
            core.gpr.r[load_slot.rt] = load_slot.value;
        }
        load_slot = {rt, value};
    }

    sr_t get_sr() const { return std::bit_cast<sr_t>(cp0.reg.n.Status); }
    void set_sr(sr_t sr) { cp0.reg.n.Status = std::bit_cast<uint32_t>(sr); }
    cause_t get_cause() const { return std::bit_cast<cause_t>(cp0.reg.n.Cause); }
    void set_cause(cause_t cause) { cp0.reg.n.Cause = std::bit_cast<uint32_t>(cause); }

    struct CP0 {
        union {
            std::array<uint32_t, 32> r{};
            CP0R n;
        } reg{};
        uint32_t istat{};
        uint32_t imask{};
    };

    struct ExceptionFrame {
        Core core;
        uint32_t sr;
        uint32_t cause;
        uint32_t epc;
        uint32_t istat{};
        uint32_t event_resume_id{};
    };

    Core core;
    CP0 cp0;
    load_delay_slot load_slot;
    branch_delay_slot branch_slot;
    MMAPR3000Bus bus;
    Bios bios;
    DMA dma;
    SPU spu;
    TimerHandler timers;
    Timing timing;
    R3000Core<c>* parent;
    std::stack<ExceptionFrame, std::vector<ExceptionFrame>> exception_frame_stack;
};

template<R3000CoreConfig c>
void R3000Core<c>::Private::set_branch_target(r3000_ptr_t target)
{
    if (branch_slot.count > 0) {
        return;
    }
    branch_slot.count = 2;
    branch_slot.target = target;
}

template<R3000CoreConfig c>
void R3000Core<c>::Private::run_instruction()
{
    HWAlignmentCheck::enable();
    ScopeGuard align_check_guard{[] { HWAlignmentCheck::disable(); }};
    // After execution of the instruction, set R0 back to 0 in case it was written.
    ScopeGuard reset_r0{[&] { core.gpr.n.r0 = 0; }};

    // Check load and branch delay slots.
    load_slot_tick();
    branch_slot_tick();

    if (auto const sr = get_sr(); (cp0.imask & cp0.istat) && sr.IEc && (sr.IntMask & 4)) {
        auto const cycles = run_exception(0);
        timing.advance_clock(cycles);
        return;
    }

    static bool done_printed = false;
    if (core.pc == 0x801b81c8 && !done_printed) {
        fmt::println("Done!");
        done_printed = true;
    }

    auto const raw_inst = read_mem<r3000_ptr_t>(core.pc);
    auto const opcode = raw_inst >> 26;

    // Advance PC.
    core.pc += sizeof(uint32_t);

    uint64_t const cycles = [&] {
        if (opcode == 0) {
            auto const [function, shift, rd, rt, rs, _] = std::bit_cast<reg_inst_t>(raw_inst);
            return run_r_inst(function, rs, rt, rd, shift);
        } else if (opcode == 1) {
            auto const [offset, function, rs, _] = std::bit_cast<imm_inst_t>(raw_inst);
            return run_bcond_inst(function, rs, offset);
        } else {
            auto const [imm, rt, rs, _] = std::bit_cast<imm_inst_t>(raw_inst);
            auto const [target, _] = std::bit_cast<jump_inst_t>(raw_inst);
            return run_ij_inst(opcode, rs, rt, imm, target);
        }
    }();

    timing.advance_clock(cycles);
    timing.run_events();

    if (dma.get_master_irq_flag()) {
        cp0.istat |= (1 << IRQ::DMA);
    }
}

template<R3000CoreConfig c>
uint64_t R3000Core<c>::Private::run_exception(uint32_t code)
{
    load_slot_flush();
    HWAlignmentCheck::disable();
    ScopeGuard enable_check{[] { HWAlignmentCheck::enable(); }};

    exception_frame_stack.push({
        .core = core,
        .sr = cp0.reg.n.Status,
        .cause = cp0.reg.n.Cause,
        .epc = cp0.reg.n.EPC,
    });
    auto& frame = exception_frame_stack.top();

    auto sr = get_sr();
    sr.IEo = sr.IEp;
    sr.KUo = sr.KUp;
    sr.IEp = sr.IEc;
    sr.KUp = sr.KUc;
    sr.IEc = 0;
    sr.KUc = 1;
    set_sr(sr);

    cause_t cause{
        .ExcCode = code,
        .IP = code == 0 ? 4u : 0u,
    };

    cp0.reg.n.EPC = core.pc;

    if (branch_slot.count > 0) {
        branch_slot.count = 0;
        cp0.reg.n.EPC -= sizeof(uint32_t);
        cause.BD = 1;
    }

    set_cause(cause);

    uint64_t total_cycles = 100;
    frame.istat = cp0.istat;
    while (frame.istat) {
        auto const bit = std::countr_zero(frame.istat);
        frame.istat &= ~(1u << bit);
        auto const ret = bios.deliverEventResumable(*parent, 0xf0000000u | (1 << bit), 0x1000, 0);
        if (ret != 0) {
            frame.event_resume_id = ret;
            return total_cycles;
        }
        total_cycles += 20;
    }

    if (bios.state.unhanled_irq_farjmp) {
        bios.longjmp(*parent, bios.state.unhanled_irq_farjmp, 1);
        total_cycles += 20;
    } else {
        total_cycles += run_returnFromException();
    }

    return total_cycles;
}

template<R3000CoreConfig c>
uint64_t R3000Core<c>::Private::run_returnFromException()
{
    if (exception_frame_stack.empty()) {
        throw CoprocessorUnusableException{};
    }

    auto const& frame = exception_frame_stack.top();

    core = frame.core;
    core.pc = cp0.reg.n.EPC;
    cp0.reg.n.Status = frame.sr;
    cp0.reg.n.Cause = frame.cause;
    cp0.reg.n.EPC = frame.epc;

    exception_frame_stack.pop();

    return 100;
}

template<R3000CoreConfig c>
uint64_t R3000Core<c>::Private::run_r_inst(uint32_t function,
                                           uint32_t rs,
                                           uint32_t rt,
                                           uint32_t rd,
                                           uint32_t shift)
{
    try {
        // clang-format off
        switch (function) {
            case 0x00: return run_r_sll    (rs, rt, rd, shift);
            case 0x01: return run_r_unk    (rs, rt, rd, shift);
            case 0x02: return run_r_srl    (rs, rt, rd, shift);
            case 0x03: return run_r_sra    (rs, rt, rd, shift);
            case 0x04: return run_r_sllv   (rs, rt, rd, shift);
            case 0x05: return run_r_unk    (rs, rt, rd, shift);
            case 0x06: return run_r_srlv   (rs, rt, rd, shift);
            case 0x07: return run_r_srav   (rs, rt, rd, shift);
            case 0x08: return run_r_jr     (rs, rt, rd, shift);
            case 0x09: return run_r_jalr   (rs, rt, rd, shift);
            case 0x0a: return run_r_unk    (rs, rt, rd, shift);
            case 0x0b: return run_r_unk    (rs, rt, rd, shift);
            case 0x0c: return run_r_syscall(rs, rt, rd, shift);
            case 0x0d: return run_r_break  (rs, rt, rd, shift);
            case 0x0e: return run_r_unk    (rs, rt, rd, shift);
            case 0x0f: return run_r_unk    (rs, rt, rd, shift);
            case 0x10: return run_r_mfhi   (rs, rt, rd, shift);
            case 0x11: return run_r_mthi   (rs, rt, rd, shift);
            case 0x12: return run_r_mflo   (rs, rt, rd, shift);
            case 0x13: return run_r_mtlo   (rs, rt, rd, shift);
            case 0x14: return run_r_unk    (rs, rt, rd, shift);
            case 0x15: return run_r_unk    (rs, rt, rd, shift);
            case 0x16: return run_r_unk    (rs, rt, rd, shift);
            case 0x17: return run_r_unk    (rs, rt, rd, shift);
            case 0x18: return run_r_mult   (rs, rt, rd, shift);
            case 0x19: return run_r_multu  (rs, rt, rd, shift);
            case 0x1a: return run_r_div    (rs, rt, rd, shift);
            case 0x1b: return run_r_divu   (rs, rt, rd, shift);
            case 0x1c: return run_r_unk    (rs, rt, rd, shift);
            case 0x1d: return run_r_unk    (rs, rt, rd, shift);
            case 0x1e: return run_r_unk    (rs, rt, rd, shift);
            case 0x1f: return run_r_unk    (rs, rt, rd, shift);
            case 0x20: return run_r_add    (rs, rt, rd, shift);
            case 0x21: return run_r_addu   (rs, rt, rd, shift);
            case 0x22: return run_r_sub    (rs, rt, rd, shift);
            case 0x23: return run_r_subu   (rs, rt, rd, shift);
            case 0x24: return run_r_and    (rs, rt, rd, shift);
            case 0x25: return run_r_or     (rs, rt, rd, shift);
            case 0x26: return run_r_xor    (rs, rt, rd, shift);
            case 0x27: return run_r_nor    (rs, rt, rd, shift);
            case 0x28: return run_r_unk    (rs, rt, rd, shift);
            case 0x29: return run_r_unk    (rs, rt, rd, shift);
            case 0x2a: return run_r_slt    (rs, rt, rd, shift);
            case 0x2b: return run_r_sltu   (rs, rt, rd, shift);
            case 0x2c: return run_r_unk    (rs, rt, rd, shift);
            case 0x2d: return run_r_unk    (rs, rt, rd, shift);
            case 0x2e: return run_r_unk    (rs, rt, rd, shift);
            case 0x2f: return run_r_unk    (rs, rt, rd, shift);
            case 0x30: return run_r_unk    (rs, rt, rd, shift);
            case 0x31: return run_r_unk    (rs, rt, rd, shift);
            case 0x32: return run_r_unk    (rs, rt, rd, shift);
            case 0x33: return run_r_unk    (rs, rt, rd, shift);
            case 0x34: return run_r_unk    (rs, rt, rd, shift);
            case 0x35: return run_r_unk    (rs, rt, rd, shift);
            case 0x36: return run_r_unk    (rs, rt, rd, shift);
            case 0x37: return run_r_unk    (rs, rt, rd, shift);
            case 0x38: return run_r_unk    (rs, rt, rd, shift);
            case 0x39: return run_r_unk    (rs, rt, rd, shift);
            case 0x3a: return run_r_unk    (rs, rt, rd, shift);
            case 0x3b: return run_r_unk    (rs, rt, rd, shift);
            case 0x3c: return run_r_unk    (rs, rt, rd, shift);
            case 0x3d: return run_r_unk    (rs, rt, rd, shift);
            case 0x3e: return run_r_unk    (rs, rt, rd, shift);
            case 0x3f: return run_r_unk    (rs, rt, rd, shift);
            // `function` is 6 bit wide, so it's impossible to receive anything higher than 0x3f (63).
            default: std::unreachable();
        }
        // clang-format on
    } catch (InstructionException& ex) {
        ex.func_code = function;
        throw;
    }
}

template<R3000CoreConfig c>
uint64_t R3000Core<c>::Private::run_bcond_inst(uint32_t function, uint32_t rs, uint32_t offset)
{
    bool const link = function & 0x10;
    // clang-format off
    switch (function & 0xf) {
        case 0: return run_bcond_bltz(rs, offset, link);
        case 1: return run_bcond_bgez(rs, offset, link);
        default: throw InstructionException{1, uint16_t(function)};
    }
    // clang-format on
}

template<R3000CoreConfig c>
uint64_t R3000Core<c>::Private::run_ij_inst(uint32_t opcode,
                                            uint32_t rs,
                                            uint32_t rt,
                                            uint32_t imm,
                                            uint32_t target)
{
    try {
        // clang-format off
    switch (opcode) {
        case 0x00: return run_ij_unk  (rs, rt, imm, target);
        case 0x01: return run_ij_unk  (rs, rt, imm, target);
        case 0x02: return run_ij_j    (rs, rt, imm, target);
        case 0x03: return run_ij_jal  (rs, rt, imm, target);
        case 0x04: return run_ij_beq  (rs, rt, imm, target);
        case 0x05: return run_ij_bne  (rs, rt, imm, target);
        case 0x06: return run_ij_blez (rs, rt, imm, target);
        case 0x07: return run_ij_bgtz (rs, rt, imm, target);
        case 0x08: return run_ij_addi (rs, rt, imm, target);
        case 0x09: return run_ij_addiu(rs, rt, imm, target);
        case 0x0a: return run_ij_slti (rs, rt, imm, target);
        case 0x0b: return run_ij_sltiu(rs, rt, imm, target);
        case 0x0c: return run_ij_andi (rs, rt, imm, target);
        case 0x0d: return run_ij_ori  (rs, rt, imm, target);
        case 0x0e: return run_ij_xori (rs, rt, imm, target);
        case 0x0f: return run_ij_lui  (rs, rt, imm, target);
        case 0x10: return run_ij_unk  (rs, rt, imm, target);
        case 0x11: return run_ij_unk  (rs, rt, imm, target);
        case 0x12: return run_ij_unk  (rs, rt, imm, target);
        case 0x13: return run_ij_unk  (rs, rt, imm, target);
        case 0x14: return run_ij_unk  (rs, rt, imm, target);
        case 0x15: return run_ij_unk  (rs, rt, imm, target);
        case 0x16: return run_ij_unk  (rs, rt, imm, target);
        case 0x17: return run_ij_unk  (rs, rt, imm, target);
        case 0x18: return run_ij_unk  (rs, rt, imm, target);
        case 0x19: return run_ij_unk  (rs, rt, imm, target);
        case 0x1a: return run_ij_unk  (rs, rt, imm, target);
        case 0x1b: return run_ij_unk  (rs, rt, imm, target);
        case 0x1c: return run_ij_unk  (rs, rt, imm, target);
        case 0x1d: return run_ij_unk  (rs, rt, imm, target);
        case 0x1e: return run_ij_unk  (rs, rt, imm, target);
        case 0x1f: return run_ij_unk  (rs, rt, imm, target);
        case 0x20: return run_ij_lb   (rs, rt, imm, target);
        case 0x21: return run_ij_lh   (rs, rt, imm, target);
        case 0x22: return run_ij_lwl  (rs, rt, imm, target);
        case 0x23: return run_ij_lw   (rs, rt, imm, target);
        case 0x24: return run_ij_lbu  (rs, rt, imm, target);
        case 0x25: return run_ij_lhu  (rs, rt, imm, target);
        case 0x26: return run_ij_lwr  (rs, rt, imm, target);
        case 0x27: return run_ij_unk  (rs, rt, imm, target);
        case 0x28: return run_ij_sb   (rs, rt, imm, target);
        case 0x29: return run_ij_sh   (rs, rt, imm, target);
        case 0x2a: return run_ij_swl  (rs, rt, imm, target);
        case 0x2b: return run_ij_sw   (rs, rt, imm, target);
        case 0x2c: return run_ij_unk  (rs, rt, imm, target);
        case 0x2d: return run_ij_unk  (rs, rt, imm, target);
        case 0x2e: return run_ij_swr  (rs, rt, imm, target);
        case 0x2f: return run_ij_unk  (rs, rt, imm, target);
        case 0x30: return run_ij_unk  (rs, rt, imm, target);
        case 0x31: return run_ij_unk  (rs, rt, imm, target);
        case 0x32: return run_ij_unk  (rs, rt, imm, target);
        case 0x33: return run_ij_unk  (rs, rt, imm, target);
        case 0x34: return run_ij_unk  (rs, rt, imm, target);
        case 0x35: return run_ij_unk  (rs, rt, imm, target);
        case 0x36: return run_ij_unk  (rs, rt, imm, target);
        case 0x37: return run_ij_unk  (rs, rt, imm, target);
        case 0x38: return run_ij_unk  (rs, rt, imm, target);
        case 0x39: return run_ij_unk  (rs, rt, imm, target);
        case 0x3a: return run_ij_unk  (rs, rt, imm, target);
        case 0x3b: return run_ij_unk  (rs, rt, imm, target);
        case 0x3c: return run_ij_unk  (rs, rt, imm, target);
        case 0x3d: return run_ij_unk  (rs, rt, imm, target);
        case 0x3e: return run_ij_unk  (rs, rt, imm, target);
        case 0x3f: return run_ij_bios (rs, rt, imm, target);
        // `opcode` is 6 bit wide, so it's impossible to receive anything higher than 0x3f (63).
        default: std::unreachable();
    };
        // clang-format on
    } catch (InstructionException& e) {
        e.opcode = opcode;
        throw;
    }
}

template<R3000CoreConfig c>
void R3000Core<c>::Private::run_branch(uint32_t offset)
{
    int32_t signed_offset = sign_extend_16(offset);
    signed_offset <<= 2;
    set_branch_target(core.pc + signed_offset);
}


template<R3000CoreConfig c>
uint64_t R3000Core<c>::Private::run_r_sll(uint32_t, uint32_t rt, uint32_t rd, uint32_t shift)
{
    core.gpr.r[rd] = core.gpr.r[rt] << shift;
    return 1;
}

template<R3000CoreConfig c>
uint64_t R3000Core<c>::Private::run_r_srl(uint32_t, uint32_t rt, uint32_t rd, uint32_t shift)
{
    core.gpr.r[rd] = core.gpr.r[rt] >> shift;
    return 1;
}

template<R3000CoreConfig c>
uint64_t R3000Core<c>::Private::run_r_sra(uint32_t, uint32_t rt, uint32_t rd, uint32_t shift)
{
    int32_t const signed_rt = core.gpr.r[rt];
    core.gpr.r[rd] = signed_rt >> shift;
    return 1;
}

template<R3000CoreConfig c>
uint64_t R3000Core<c>::Private::run_r_sllv(uint32_t rs, uint32_t rt, uint32_t rd, uint32_t)
{
    core.gpr.r[rd] = core.gpr.r[rt] << (core.gpr.r[rs] & 0x1f);
    return 1;
}

template<R3000CoreConfig c>
uint64_t R3000Core<c>::Private::run_r_srlv(uint32_t rs, uint32_t rt, uint32_t rd, uint32_t)
{
    core.gpr.r[rd] = core.gpr.r[rt] >> (core.gpr.r[rs] & 0x1f);
    return 1;
}

template<R3000CoreConfig c>
uint64_t R3000Core<c>::Private::run_r_srav(uint32_t rs, uint32_t rt, uint32_t rd, uint32_t)
{
    int32_t const signed_rt = core.gpr.r[rt];
    core.gpr.r[rd] = signed_rt >> (core.gpr.r[rs] & 0x1f);
    return 1;
}

template<R3000CoreConfig c>
uint64_t R3000Core<c>::Private::run_r_jr(uint32_t rs, uint32_t, uint32_t, uint32_t)
{
    set_branch_target(core.gpr.r[rs]);
    return 1;
}

template<R3000CoreConfig c>
uint64_t R3000Core<c>::Private::run_r_jalr(uint32_t rs, uint32_t, uint32_t rd, uint32_t)
{
    // TODO: this instruction traps immediately when core.gpr.r[rs] is an invalid address, and not
    // after the delay slot is executed like other branch instructions.
    core.gpr.r[rd] = core.pc + sizeof(uint32_t);
    set_branch_target(core.gpr.r[rs]);
    return 1;
}

template<R3000CoreConfig c>
uint64_t R3000Core<c>::Private::run_r_syscall(uint32_t, uint32_t, uint32_t, uint32_t)
{
    load_slot_flush();
    auto const flags = std::bit_cast<uint32_t>(sr_t{
        // It should be IEp, not IEc, but we are not emulating the transition to interrupt and thus
        // the IEc/KUc/IEp/KUp flags are not shifted.
        .IEc = 1,
        .IntMask = 4,
    });
    switch (core.gpr.n.a0) {
    case 0: // Do nothing.
        break;
    case 1: // enterCriticalSection
        core.gpr.n.v0 = (cp0.reg.n.Status & flags) == flags;
        cp0.reg.n.Status &= ~flags;
        break;
    case 2: // leaveCriticalSection
        cp0.reg.n.Status |= flags;
        break;
    case 3: // unimplemented
        core.gpr.n.v0 = 1;
        break;
    default: // deliverEvent - unimplemented
        break;
    }
    return 100;
}

template<R3000CoreConfig c>
uint64_t R3000Core<c>::Private::run_r_break(uint32_t, uint32_t, uint32_t, uint32_t)
{
    // Not supported yet.
    throw InstructionException{};
}

template<R3000CoreConfig c>
uint64_t R3000Core<c>::Private::run_r_mfhi(uint32_t, uint32_t, uint32_t rd, uint32_t)
{
    core.gpr.r[rd] = core.hi;
    return 1;
}

template<R3000CoreConfig c>
uint64_t R3000Core<c>::Private::run_r_mthi(uint32_t rs, uint32_t, uint32_t, uint32_t)
{
    core.hi = core.gpr.r[rs];
    return 1;
}

template<R3000CoreConfig c>
uint64_t R3000Core<c>::Private::run_r_mflo(uint32_t, uint32_t, uint32_t rd, uint32_t)
{
    core.gpr.r[rd] = core.lo;
    return 1;
}

template<R3000CoreConfig c>
uint64_t R3000Core<c>::Private::run_r_mtlo(uint32_t rs, uint32_t, uint32_t, uint32_t)
{
    core.lo = core.gpr.r[rs];
    return 1;
}

template<R3000CoreConfig c>
uint64_t R3000Core<c>::Private::run_r_mult(uint32_t rs, uint32_t rt, uint32_t, uint32_t)
{
    int32_t const signed_rs = core.gpr.r[rs];
    int32_t const signed_rt = core.gpr.r[rt];
    uint64_t const result = (int64_t)signed_rs * signed_rt;
    core.lo = result;
    core.hi = result >> 32;
    return 1;
}

template<R3000CoreConfig c>
uint64_t R3000Core<c>::Private::run_r_multu(uint32_t rs, uint32_t rt, uint32_t, uint32_t)
{
    uint64_t const result = (uint64_t)core.gpr.r[rs] * core.gpr.r[rt];
    core.lo = result;
    core.hi = result >> 32;
    return 1;
}

template<R3000CoreConfig c>
uint64_t R3000Core<c>::Private::run_r_div(uint32_t rs, uint32_t rt, uint32_t, uint32_t)
{
    int32_t const signed_rs = core.gpr.r[rs];
    int32_t const signed_rt = core.gpr.r[rt];
    if (signed_rt == 0) {
        core.lo = signed_rs > 0 ? -1 : 1;
        core.hi = signed_rs;
    } else if (signed_rs == std::numeric_limits<int32_t>::min() && signed_rt == -1) {
        core.lo = std::numeric_limits<int32_t>::min();
        core.hi = 0;
    } else {
        core.lo = signed_rs / signed_rt;
        core.hi = signed_rs % signed_rt;
    }
    return 1;
}

template<R3000CoreConfig c>
uint64_t R3000Core<c>::Private::run_r_divu(uint32_t rs, uint32_t rt, uint32_t, uint32_t)
{
    if (core.gpr.r[rt] == 0) {
        core.lo = 0xffffffff;
        core.hi = core.gpr.r[rs];
    } else {
        core.lo = core.gpr.r[rs] / core.gpr.r[rt];
        core.hi = core.gpr.r[rs] % core.gpr.r[rt];
    }
    return 1;
}

template<R3000CoreConfig c>
uint64_t R3000Core<c>::Private::run_r_add(uint32_t rs, uint32_t rt, uint32_t rd, uint32_t)
{
    int32_t const signed_rs = core.gpr.r[rs];
    int32_t const signed_rt = core.gpr.r[rt];
    int32_t result;
    auto const overflow = ckd_add(&result, signed_rs, signed_rt);
    if (overflow) {
        HWAlignmentCheck::disable();
        throw OverflowException{};
    }
    core.gpr.r[rd] = result;
    return 1;
}

template<R3000CoreConfig c>
uint64_t R3000Core<c>::Private::run_r_addu(uint32_t rs, uint32_t rt, uint32_t rd, uint32_t)
{
    core.gpr.r[rd] = core.gpr.r[rs] + core.gpr.r[rt];
    return 1;
}

template<R3000CoreConfig c>
uint64_t R3000Core<c>::Private::run_r_sub(uint32_t rs, uint32_t rt, uint32_t rd, uint32_t)
{
    int32_t const signed_rs = core.gpr.r[rs];
    int32_t const signed_rt = core.gpr.r[rt];
    int32_t result;
    auto const overflow = ckd_sub(&result, signed_rs, signed_rt);
    if (overflow) {
        HWAlignmentCheck::disable();
        throw OverflowException{};
    }
    core.gpr.r[rd] = result;
    return 1;
}

template<R3000CoreConfig c>
uint64_t R3000Core<c>::Private::run_r_subu(uint32_t rs, uint32_t rt, uint32_t rd, uint32_t)
{
    core.gpr.r[rd] = core.gpr.r[rs] - core.gpr.r[rt];
    return 1;
}

template<R3000CoreConfig c>
uint64_t R3000Core<c>::Private::run_r_and(uint32_t rs, uint32_t rt, uint32_t rd, uint32_t)
{
    core.gpr.r[rd] = core.gpr.r[rs] & core.gpr.r[rt];
    return 1;
}

template<R3000CoreConfig c>
uint64_t R3000Core<c>::Private::run_r_or(uint32_t rs, uint32_t rt, uint32_t rd, uint32_t)
{
    core.gpr.r[rd] = core.gpr.r[rs] | core.gpr.r[rt];
    return 1;
}

template<R3000CoreConfig c>
uint64_t R3000Core<c>::Private::run_r_xor(uint32_t rs, uint32_t rt, uint32_t rd, uint32_t)
{
    core.gpr.r[rd] = core.gpr.r[rs] ^ core.gpr.r[rt];
    return 1;
}

template<R3000CoreConfig c>
uint64_t R3000Core<c>::Private::run_r_nor(uint32_t rs, uint32_t rt, uint32_t rd, uint32_t)
{
    core.gpr.r[rd] = ~(core.gpr.r[rs] | core.gpr.r[rt]);
    return 1;
}

template<R3000CoreConfig c>
uint64_t R3000Core<c>::Private::run_r_slt(uint32_t rs, uint32_t rt, uint32_t rd, uint32_t)
{
    int32_t const signed_rs = core.gpr.r[rs];
    int32_t const signed_rt = core.gpr.r[rt];
    core.gpr.r[rd] = signed_rs < signed_rt;
    return 1;
}

template<R3000CoreConfig c>
uint64_t R3000Core<c>::Private::run_r_sltu(uint32_t rs, uint32_t rt, uint32_t rd, uint32_t)
{
    core.gpr.r[rd] = core.gpr.r[rs] < core.gpr.r[rt];
    return 1;
}

template<R3000CoreConfig c>
uint64_t R3000Core<c>::Private::run_r_unk(uint32_t, uint32_t, uint32_t, uint32_t)
{
    throw InstructionException{};
}

template<R3000CoreConfig c>
uint64_t R3000Core<c>::Private::run_bcond_bltz(uint32_t rs, uint32_t offset, bool link)
{
    int32_t const signed_rs = core.gpr.r[rs];
    if (signed_rs < 0) {
        run_branch(offset);
    }
    if (link) {
        core.gpr.n.ra = core.pc + sizeof(uint32_t);
    }
    return 1;
}

template<R3000CoreConfig c>
uint64_t R3000Core<c>::Private::run_bcond_bgez(uint32_t rs, uint32_t offset, bool link)
{
    int32_t const signed_rs = core.gpr.r[rs];
    if (signed_rs >= 0) {
        run_branch(offset);
    }
    if (link) {
        core.gpr.n.ra = core.pc + sizeof(uint32_t);
    }
    return 1;
}

template<R3000CoreConfig c>
uint64_t R3000Core<c>::Private::run_ij_j(uint32_t, uint32_t, uint32_t, uint32_t target)
{
    auto const target_addr = (core.pc & 0xfc000000u) | (target << 2);
    set_branch_target(target_addr);
    return 1;
}

template<R3000CoreConfig c>
uint64_t R3000Core<c>::Private::run_ij_jal(uint32_t, uint32_t, uint32_t, uint32_t target)
{
    core.gpr.n.ra = core.pc + sizeof(uint32_t);
    auto const target_addr = (core.pc & 0xfc000000u) | (target << 2);
    set_branch_target(target_addr);
    return 1;
}

template<R3000CoreConfig c>
uint64_t R3000Core<c>::Private::run_ij_beq(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t)
{
    if (core.gpr.r[rs] == core.gpr.r[rt]) {
        run_branch(imm);
    }
    return 1;
}

template<R3000CoreConfig c>
uint64_t R3000Core<c>::Private::run_ij_bne(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t)
{
    if (core.gpr.r[rs] != core.gpr.r[rt]) {
        run_branch(imm);
    }
    return 1;
}

template<R3000CoreConfig c>
uint64_t R3000Core<c>::Private::run_ij_blez(uint32_t rs, uint32_t, uint32_t imm, uint32_t)
{
    int32_t const signed_rs = core.gpr.r[rs];
    if (signed_rs <= 0) {
        run_branch(imm);
    }
    return 1;
}

template<R3000CoreConfig c>
uint64_t R3000Core<c>::Private::run_ij_bgtz(uint32_t rs, uint32_t, uint32_t imm, uint32_t)
{
    int32_t const signed_rs = core.gpr.r[rs];
    if (signed_rs > 0) {
        run_branch(imm);
    }
    return 1;
}

template<R3000CoreConfig c>
uint64_t R3000Core<c>::Private::run_ij_addi(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t)
{
    int32_t const signed_rs = core.gpr.r[rs];
    int32_t const signed_imm = sign_extend_16(imm);
    int32_t result;
    auto const overflow = ckd_add(&result, signed_rs, signed_imm);
    if (overflow) {
        HWAlignmentCheck::disable();
        throw OverflowException{};
    }
    core.gpr.r[rt] = result;
    return 1;
}

template<R3000CoreConfig c>
uint64_t R3000Core<c>::Private::run_ij_addiu(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t)
{
    core.gpr.r[rt] = core.gpr.r[rs] + sign_extend_16(imm);
    return 1;
}

template<R3000CoreConfig c>
uint64_t R3000Core<c>::Private::run_ij_slti(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t)
{
    int32_t const signed_rs = core.gpr.r[rs];
    int32_t const signed_imm = sign_extend_16(imm);
    core.gpr.r[rt] = signed_rs < signed_imm;
    return 1;
}

template<R3000CoreConfig c>
uint64_t R3000Core<c>::Private::run_ij_sltiu(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t)
{
    core.gpr.r[rt] = core.gpr.r[rs] < sign_extend_16(imm);
    return 1;
}

template<R3000CoreConfig c>
uint64_t R3000Core<c>::Private::run_ij_andi(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t)
{
    core.gpr.r[rt] = core.gpr.r[rs] & imm;
    return 1;
}

template<R3000CoreConfig c>
uint64_t R3000Core<c>::Private::run_ij_ori(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t)
{
    core.gpr.r[rt] = core.gpr.r[rs] | imm;
    return 1;
}

template<R3000CoreConfig c>
uint64_t R3000Core<c>::Private::run_ij_xori(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t)
{
    core.gpr.r[rt] = core.gpr.r[rs] ^ imm;
    return 1;
}

template<R3000CoreConfig c>
uint64_t R3000Core<c>::Private::run_ij_lui(uint32_t, uint32_t rt, uint32_t imm, uint32_t)
{
    core.gpr.r[rt] = imm << 16;
    return 1;
}

template<R3000CoreConfig c>
uint64_t R3000Core<c>::Private::run_ij_lb(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t)
{
    load(rt, (int8_t)read_mem<uint8_t>(core.gpr.r[rs] + sign_extend_16(imm)));
    return 1;
}

template<R3000CoreConfig c>
uint64_t R3000Core<c>::Private::run_ij_lh(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t)
{
    load(rt, (int16_t)read_mem<uint16_t>(core.gpr.r[rs] + sign_extend_16(imm)));
    return 1;
}

template<R3000CoreConfig c>
uint64_t R3000Core<c>::Private::run_ij_lw(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t)
{
    load(rt, read_mem<uint32_t>(core.gpr.r[rs] + sign_extend_16(imm)));
    return 1;
}

template<R3000CoreConfig c>
uint64_t R3000Core<c>::Private::run_ij_lbu(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t)
{
    load(rt, read_mem<uint8_t>(core.gpr.r[rs] + sign_extend_16(imm)));
    return 1;
}

template<R3000CoreConfig c>
uint64_t R3000Core<c>::Private::run_ij_lhu(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t)
{
    load(rt, read_mem<uint16_t>(core.gpr.r[rs] + sign_extend_16(imm)));
    return 1;
}

template<R3000CoreConfig c>
uint64_t R3000Core<c>::Private::run_ij_lwl(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t)
{
    auto const prev_value =
        load_slot.enabled && load_slot.rt == rt ? load_slot.value : core.gpr.r[rt];
    auto const addr = core.gpr.r[rs] + sign_extend_16(imm);
    auto const misalignment = addr & 0x3;
    auto const aligned_addr = addr & ~0x3;
    auto const word = read_mem<uint32_t>(aligned_addr);
    auto const mask = 0x00ffffffu >> (misalignment * 8);
    auto const result = (prev_value & mask) | (word << ((3 - misalignment) * 8));
    load(rt, result);
    return 1;
}

template<R3000CoreConfig c>
uint64_t R3000Core<c>::Private::run_ij_lwr(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t)
{
    auto const prev_value =
        load_slot.enabled && load_slot.rt == rt ? load_slot.value : core.gpr.r[rt];
    auto const addr = core.gpr.r[rs] + sign_extend_16(imm);
    auto const misalignment = addr & 0x3;
    auto const aligned_addr = addr & ~0x3;
    auto const word = read_mem<uint32_t>(aligned_addr);
    auto const mask = 0xffffff00u << ((3 - misalignment) * 8);
    auto const result = (prev_value & mask) | (word >> (misalignment * 8));
    load(rt, result);
    return 1;
}

template<R3000CoreConfig c>
uint64_t R3000Core<c>::Private::run_ij_sb(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t)
{
    write_mem<uint8_t>(core.gpr.r[rs] + sign_extend_16(imm), core.gpr.r[rt]);
    return 1;
}

template<R3000CoreConfig c>
uint64_t R3000Core<c>::Private::run_ij_sh(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t)
{
    write_mem<uint16_t>(core.gpr.r[rs] + sign_extend_16(imm), core.gpr.r[rt]);
    return 1;
}

template<R3000CoreConfig c>
uint64_t R3000Core<c>::Private::run_ij_sw(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t)
{
    write_mem<uint32_t>(core.gpr.r[rs] + sign_extend_16(imm), core.gpr.r[rt]);
    return 1;
}

template<R3000CoreConfig c>
uint64_t R3000Core<c>::Private::run_ij_swl(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t)
{
    auto unaligned_ptr = core.gpr.r[rs] + sign_extend_16(imm);
    auto value = core.gpr.r[rt];
    do {
        write_mem<uint8_t>(unaligned_ptr, value >> 24);
        unaligned_ptr -= 1;
        value <<= 8;
    } while ((unaligned_ptr & 0x3) != 3);
    return 1;
}

template<R3000CoreConfig c>
uint64_t R3000Core<c>::Private::run_ij_swr(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t)
{
    auto unaligned_ptr = core.gpr.r[rs] + sign_extend_16(imm);
    auto value = core.gpr.r[rt];
    do {
        write_mem<uint8_t>(unaligned_ptr, value);
        unaligned_ptr += 1;
        value >>= 8;
    } while (unaligned_ptr & 0x3);
    return 1;
}

template<R3000CoreConfig c>
uint64_t R3000Core<c>::Private::run_ij_bios(uint32_t, uint32_t, uint32_t imm, uint32_t)
{
    load_slot_flush();
    HWAlignmentCheck::disable();
    bios.run_bios_fn(*parent, imm);
    HWAlignmentCheck::enable();
    // Return a unified 100 cycles for the emulated HLE function call.
    return 100;
}

template<R3000CoreConfig c>
uint64_t R3000Core<c>::Private::run_ij_unk(uint32_t, uint32_t, uint32_t, uint32_t)
{
    throw InstructionException{};
}

template<R3000CoreConfig c>
R3000Core<c>::R3000Core()
    : p{std::make_unique<Private>(this)}
{
    p->bus.init(this);
    p->dma.init(this);
    p->spu.init(this);
    p->timers.init(this);
}

template<R3000CoreConfig c>
R3000Core<c>::~R3000Core() = default;

template<R3000CoreConfig c>
void R3000Core<c>::run()
{
    p->run_instruction();
}

template<R3000CoreConfig c>
void R3000Core<c>::set_regs(uint32_t sp, uint32_t pc)
{
    p->core.pc = pc;
    p->core.gpr.n.sp = sp;
}

template<R3000CoreConfig c>
uint8_t R3000Core<c>::read_mem_u8(r3000_ptr_t addr) const
{
    return p->template read_mem<uint8_t>(addr);
}

template<R3000CoreConfig c>
uint16_t R3000Core<c>::read_mem_u16(r3000_ptr_t addr) const
{
    return p->template read_mem<uint16_t>(addr);
}

template<R3000CoreConfig c>
uint32_t R3000Core<c>::read_mem_u32(r3000_ptr_t addr) const
{
    return p->template read_mem<uint32_t>(addr);
}

template<R3000CoreConfig c>
void R3000Core<c>::write_mem_u8(r3000_ptr_t addr, uint8_t value)
{
    p->write_mem(addr, value);
}

template<R3000CoreConfig c>
void R3000Core<c>::write_mem_u16(r3000_ptr_t addr, uint16_t value)
{
    p->write_mem(addr, value);
}

template<R3000CoreConfig c>
void R3000Core<c>::write_mem_u32(r3000_ptr_t addr, uint32_t value)
{
    p->write_mem(addr, value);
}

template<R3000CoreConfig c>
Core& R3000Core<c>::core()
{
    return p->core;
}

template<R3000CoreConfig c>
Core const& R3000Core<c>::core() const
{
    return p->core;
}

template<R3000CoreConfig c>
uint8_t* R3000Core<c>::get_buffer_checked(r3000_ptr_t addr, uint32_t size, bool ram) const
{
    using boost::container::static_vector;

    auto const [max_size, base_addr, ptr, prefixes] =
        [&] -> std::tuple<uint32_t, uint32_t, uint8_t*, static_vector<uint32_t, 3>> {
        if (ram) {
            return {0x200000u, 0, p->bus.get_mem_ptr(), {0x0u, 0x80000000u, 0xa0000000u}};
        } else {
            // For devices/scratchpad, only allow the lower mirror (0x1f800000).
            return {0x3000u, HWReg::DEVICE_BASE, p->bus.get_device_mem_ptr(), {0x1f800000u}};
        }
    }();

    if (size > max_size) {
        throw AddressException{addr + size};
    }

    // Check that the base address is within any of the regions.
    if (auto const addr_prefix = addr & 0xffe00000u;
        !std::ranges::contains(prefixes, addr_prefix)) {
        throw AddressException{addr};
    }

    // Lastly, check that addr + size lies within the limits.
    if (auto const addr_suffix = addr & (max_size - 1); addr_suffix + size > max_size) {
        throw AddressException{addr + size};
    }

    return ptr + (addr - base_addr);
}

template<R3000CoreConfig c>
uint32_t& R3000Core<c>::istat()
{
    return p->cp0.istat;
}

template<R3000CoreConfig c>
uint32_t& R3000Core<c>::imask()
{
    return p->cp0.imask;
}

template<R3000CoreConfig c>
void R3000Core<c>::return_from_exception()
{
    p->run_returnFromException();
}

template<R3000CoreConfig c>
Bios* R3000Core<c>::get_bios()
{
    return &p->bios;
}

template<R3000CoreConfig c>
DMA* R3000Core<c>::get_dma()
{
    return &p->dma;
}

template<R3000CoreConfig c>
SPU* R3000Core<c>::get_spu()
{
    return &p->spu;
}

template<R3000CoreConfig c>
TimerHandler* R3000Core<c>::get_timers()
{
    return &p->timers;
}

template<R3000CoreConfig c>
Timing* R3000Core<c>::get_timing()
{
    return &p->timing;
}

std::unique_ptr<R3000> R3000::build()
{
    return std::make_unique<R3000Core<{FaultCheck::HARDWARE, AlignmentCheck::NO_CHECK}>>();
}
