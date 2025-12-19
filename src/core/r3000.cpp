#include "r3000-impl.h"
#include "mmap-r3000bus.h"
#include "bios.h"

#include <fmt/format.h>

#include <cstdint>
#include <concepts>
#include <bit>
#include <utility>
#include <algorithm>
#include <stdckdint.h>

namespace {

struct reg_inst_t {
    uint32_t function : 6;
    uint32_t shift : 5;
    uint32_t rd : 5;
    uint32_t rt : 5;
    uint32_t rs : 5;
    uint32_t opcode : 6;
};

struct imm_inst_t {
    uint32_t imm : 16;
    uint32_t rt : 5;
    uint32_t rs : 5;
    uint32_t opcode : 6;
};

struct jump_inst_t {
    uint32_t target : 26;
    uint32_t opcode : 6;
};

static_assert(sizeof(reg_inst_t) == sizeof(uint32_t));
static_assert(sizeof(imm_inst_t) == sizeof(uint32_t));
static_assert(sizeof(jump_inst_t) == sizeof(uint32_t));

uint32_t sign_extend_16(uint32_t x)
{
    return (int16_t)x;
}

} // namespace

template<R3000CoreConfig c>
struct R3000Core<c>::Private {
    explicit Private(R3000Core<c>* parent)
        : bus{parent}
        , parent{parent}
    {
        load_slot.enabled = 0;
    }

    struct HWAlignmentCheck {
        static void enable()
        {
            if constexpr (c.alignment_check == AlignmentCheck::HARDWARE) {
                asm("pushf\n"
                    "orl $0x40000, (%%rsp)\n"
                    "popf\n" ::
                        : "memory");
            }
        }

        static void disable()
        {
            if constexpr (c.alignment_check == AlignmentCheck::HARDWARE) {
                asm("pushf\n"
                    "andl $~0x40000, (%%rsp)\n"
                    "popf\n" ::
                        : "memory");
            }
        }
    };

    struct [[nodiscard]] HWAlignmentCheckGuard {
        explicit HWAlignmentCheckGuard() { HWAlignmentCheck::enable(); }
        ~HWAlignmentCheckGuard() { HWAlignmentCheck::disable(); }
    };

    void run_instruction();

    void set_branch_target(r3000_ptr_t target);

    void run_r_inst(uint32_t function, uint32_t rs, uint32_t rt, uint32_t rd, uint32_t shift);
    void run_bcond_inst(uint32_t function, uint32_t rs, uint32_t offset);
    void run_ij_inst(uint32_t opcode, uint32_t rs, uint32_t rt, uint32_t imm, uint32_t target);
    void run_branch(uint32_t offset);

    void run_r_sll(uint32_t rs, uint32_t rt, uint32_t rd, uint32_t shift);
    void run_r_srl(uint32_t rs, uint32_t rt, uint32_t rd, uint32_t shift);
    void run_r_sra(uint32_t rs, uint32_t rt, uint32_t rd, uint32_t shift);
    void run_r_sllv(uint32_t rs, uint32_t rt, uint32_t rd, uint32_t shift);
    void run_r_srlv(uint32_t rs, uint32_t rt, uint32_t rd, uint32_t shift);
    void run_r_srav(uint32_t rs, uint32_t rt, uint32_t rd, uint32_t shift);
    void run_r_jr(uint32_t rs, uint32_t rt, uint32_t rd, uint32_t shift);
    void run_r_jalr(uint32_t rs, uint32_t rt, uint32_t rd, uint32_t shift);
    void run_r_syscall(uint32_t rs, uint32_t rt, uint32_t rd, uint32_t shift);
    void run_r_break(uint32_t rs, uint32_t rt, uint32_t rd, uint32_t shift);
    void run_r_mfhi(uint32_t rs, uint32_t rt, uint32_t rd, uint32_t shift);
    void run_r_mthi(uint32_t rs, uint32_t rt, uint32_t rd, uint32_t shift);
    void run_r_mflo(uint32_t rs, uint32_t rt, uint32_t rd, uint32_t shift);
    void run_r_mtlo(uint32_t rs, uint32_t rt, uint32_t rd, uint32_t shift);
    void run_r_mult(uint32_t rs, uint32_t rt, uint32_t rd, uint32_t shift);
    void run_r_multu(uint32_t rs, uint32_t rt, uint32_t rd, uint32_t shift);
    void run_r_div(uint32_t rs, uint32_t rt, uint32_t rd, uint32_t shift);
    void run_r_divu(uint32_t rs, uint32_t rt, uint32_t rd, uint32_t shift);
    void run_r_add(uint32_t rs, uint32_t rt, uint32_t rd, uint32_t shift);
    void run_r_addu(uint32_t rs, uint32_t rt, uint32_t rd, uint32_t shift);
    void run_r_sub(uint32_t rs, uint32_t rt, uint32_t rd, uint32_t shift);
    void run_r_subu(uint32_t rs, uint32_t rt, uint32_t rd, uint32_t shift);
    void run_r_and(uint32_t rs, uint32_t rt, uint32_t rd, uint32_t shift);
    void run_r_or(uint32_t rs, uint32_t rt, uint32_t rd, uint32_t shift);
    void run_r_xor(uint32_t rs, uint32_t rt, uint32_t rd, uint32_t shift);
    void run_r_nor(uint32_t rs, uint32_t rt, uint32_t rd, uint32_t shift);
    void run_r_slt(uint32_t rs, uint32_t rt, uint32_t rd, uint32_t shift);
    void run_r_sltu(uint32_t rs, uint32_t rt, uint32_t rd, uint32_t shift);
    void run_r_unk(uint32_t rs, uint32_t rt, uint32_t rd, uint32_t shift);

    void run_bcond_bltz(uint32_t rs, uint32_t offset, bool link);
    void run_bcond_bgez(uint32_t rs, uint32_t offset, bool link);

    void run_ij_j(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t target);
    void run_ij_jal(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t target);
    void run_ij_beq(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t target);
    void run_ij_bne(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t target);
    void run_ij_blez(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t target);
    void run_ij_bgtz(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t target);
    void run_ij_addi(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t target);
    void run_ij_addiu(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t target);
    void run_ij_slti(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t target);
    void run_ij_sltiu(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t target);
    void run_ij_andi(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t target);
    void run_ij_ori(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t target);
    void run_ij_xori(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t target);
    void run_ij_lui(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t target);
    void run_ij_lb(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t target);
    void run_ij_lh(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t target);
    void run_ij_lw(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t target);
    void run_ij_lbu(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t target);
    void run_ij_lhu(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t target);
    void run_ij_lwl(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t target);
    void run_ij_lwr(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t target);
    void run_ij_sb(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t target);
    void run_ij_sh(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t target);
    void run_ij_sw(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t target);
    void run_ij_swl(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t target);
    void run_ij_swr(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t target);
    void run_ij_bios(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t target);
    void run_ij_unk(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t target);

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
            core.gpr[load_slot.rt] = load_slot.value;
        }
    }

    void load_slot_flush()
    {
        if (!load_slot.enabled) {
            return;
        }
        load_slot.enabled = 0;
        core.gpr[load_slot.rt] = load_slot.value;
    }

    void load(uint32_t rt, uint32_t value)
    {
        if (load_slot.enabled) {
            core.gpr[load_slot.rt] = load_slot.value;
        }
        load_slot = {rt, value};
    }

    Core core;
    load_delay_slot load_slot;
    branch_delay_slot branch_slot;
    MMAPR3000Bus bus;
    Bios bios;
    R3000Core<c>* parent;
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
    HWAlignmentCheckGuard align_check_guard{};

    // Check load delay slot.
    load_slot_tick();

    // Check branch slot
    if (branch_slot.count > 0 && --branch_slot.count == 0) {
        core.pc = branch_slot.target;
    }

    auto const raw_inst = read_mem<r3000_ptr_t>(core.pc);
    auto const opcode = raw_inst >> 26;

    if (opcode == 0) {
        auto const [function, shift, rd, rt, rs, _] = std::bit_cast<reg_inst_t>(raw_inst);
        run_r_inst(function, rs, rt, rd, shift);
    } else if (opcode == 1) {
        auto const [offset, function, rs, _] = std::bit_cast<imm_inst_t>(raw_inst);
        run_bcond_inst(function, rs, offset);
    } else {
        auto const [imm, rt, rs, _] = std::bit_cast<imm_inst_t>(raw_inst);
        auto const [target, _] = std::bit_cast<jump_inst_t>(raw_inst);
        run_ij_inst(opcode, rs, rt, imm, target);
    }

    // After execution of the instruction, set R0 back to 0 in case it was written.
    core.gpr[GPRName::r0] = 0;

    // Advance PC.
    core.pc += sizeof(uint32_t);
}

template<R3000CoreConfig c>
void R3000Core<c>::Private::run_r_inst(uint32_t function,
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
            default: return std::unreachable();
        }
        // clang-format on
    } catch (InstructionException& ex) {
        ex.func_code = function;
        throw;
    }
}

template<R3000CoreConfig c>
void R3000Core<c>::Private::run_bcond_inst(uint32_t function, uint32_t rs, uint32_t offset)
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
void R3000Core<c>::Private::run_ij_inst(uint32_t opcode,
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
        default: return std::unreachable();
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
    set_branch_target(core.pc + sizeof(uint32_t) + signed_offset);
}


template<R3000CoreConfig c>
void R3000Core<c>::Private::run_r_sll(uint32_t, uint32_t rt, uint32_t rd, uint32_t shift)
{
    core.gpr[rd] = core.gpr[rt] << shift;
}

template<R3000CoreConfig c>
void R3000Core<c>::Private::run_r_srl(uint32_t, uint32_t rt, uint32_t rd, uint32_t shift)
{
    core.gpr[rd] = core.gpr[rt] >> shift;
}

template<R3000CoreConfig c>
void R3000Core<c>::Private::run_r_sra(uint32_t, uint32_t rt, uint32_t rd, uint32_t shift)
{
    int32_t const signed_rt = core.gpr[rt];
    core.gpr[rd] = signed_rt >> shift;
}

template<R3000CoreConfig c>
void R3000Core<c>::Private::run_r_sllv(uint32_t rs, uint32_t rt, uint32_t rd, uint32_t)
{
    core.gpr[rd] = core.gpr[rt] << (core.gpr[rs] & 0x1f);
}

template<R3000CoreConfig c>
void R3000Core<c>::Private::run_r_srlv(uint32_t rs, uint32_t rt, uint32_t rd, uint32_t)
{
    core.gpr[rd] = core.gpr[rt] >> (core.gpr[rs] & 0x1f);
}

template<R3000CoreConfig c>
void R3000Core<c>::Private::run_r_srav(uint32_t rs, uint32_t rt, uint32_t rd, uint32_t)
{
    int32_t const signed_rt = core.gpr[rt];
    core.gpr[rd] = signed_rt >> (core.gpr[rs] & 0x1f);
}

template<R3000CoreConfig c>
void R3000Core<c>::Private::run_r_jr(uint32_t rs, uint32_t, uint32_t, uint32_t)
{
    set_branch_target(core.gpr[rs]);
}

template<R3000CoreConfig c>
void R3000Core<c>::Private::run_r_jalr(uint32_t rs, uint32_t, uint32_t rd, uint32_t)
{
    // TODO: this instruction traps immediately when core.gpr[rs] is an invalid address, and not
    // after the delay slot is executed like other branch instructions.
    core.gpr[rd] = core.pc + sizeof(uint32_t) * 2;
    set_branch_target(core.gpr[rs]);
}

template<R3000CoreConfig c>
void R3000Core<c>::Private::run_r_syscall(uint32_t, uint32_t, uint32_t, uint32_t)
{
    // Not supported yet.
    //throw InstructionException{};
}

template<R3000CoreConfig c>
void R3000Core<c>::Private::run_r_break(uint32_t, uint32_t, uint32_t, uint32_t)
{
    // Not supported yet.
    throw InstructionException{};
}

template<R3000CoreConfig c>
void R3000Core<c>::Private::run_r_mfhi(uint32_t, uint32_t, uint32_t rd, uint32_t)
{
    core.gpr[rd] = core.hi;
}

template<R3000CoreConfig c>
void R3000Core<c>::Private::run_r_mthi(uint32_t rs, uint32_t, uint32_t, uint32_t)
{
    core.hi = core.gpr[rs];
}

template<R3000CoreConfig c>
void R3000Core<c>::Private::run_r_mflo(uint32_t, uint32_t, uint32_t rd, uint32_t)
{
    core.gpr[rd] = core.lo;
}

template<R3000CoreConfig c>
void R3000Core<c>::Private::run_r_mtlo(uint32_t rs, uint32_t, uint32_t, uint32_t)
{
    core.lo = core.gpr[rs];
}

template<R3000CoreConfig c>
void R3000Core<c>::Private::run_r_mult(uint32_t rs, uint32_t rt, uint32_t, uint32_t)
{
    int32_t const signed_rs = core.gpr[rs];
    int32_t const signed_rt = core.gpr[rt];
    uint64_t const result = (int64_t)signed_rs * signed_rt;
    core.lo = result;
    core.hi = result >> 32;
}

template<R3000CoreConfig c>
void R3000Core<c>::Private::run_r_multu(uint32_t rs, uint32_t rt, uint32_t, uint32_t)
{
    uint64_t const result = (uint64_t)core.gpr[rs] * core.gpr[rt];
    core.lo = result;
    core.hi = result >> 32;
}

template<R3000CoreConfig c>
void R3000Core<c>::Private::run_r_div(uint32_t rs, uint32_t rt, uint32_t, uint32_t)
{
    int32_t const signed_rs = core.gpr[rs];
    int32_t const signed_rt = core.gpr[rt];
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
}

template<R3000CoreConfig c>
void R3000Core<c>::Private::run_r_divu(uint32_t rs, uint32_t rt, uint32_t, uint32_t)
{
    if (core.gpr[rt] == 0) {
        core.lo = 0xffffffff;
        core.hi = core.gpr[rs];
    } else {
        core.lo = core.gpr[rs] / core.gpr[rt];
        core.hi = core.gpr[rs] % core.gpr[rt];
    }
}

template<R3000CoreConfig c>
void R3000Core<c>::Private::run_r_add(uint32_t rs, uint32_t rt, uint32_t rd, uint32_t)
{
    int32_t const signed_rs = core.gpr[rs];
    int32_t const signed_rt = core.gpr[rt];
    int32_t result;
    auto const overflow = ckd_add(&result, signed_rs, signed_rt);
    if (overflow) {
        HWAlignmentCheck::disable();
        throw OverflowException{};
    }
    core.gpr[rd] = result;
}

template<R3000CoreConfig c>
void R3000Core<c>::Private::run_r_addu(uint32_t rs, uint32_t rt, uint32_t rd, uint32_t)
{
    core.gpr[rd] = core.gpr[rs] + core.gpr[rt];
}

template<R3000CoreConfig c>
void R3000Core<c>::Private::run_r_sub(uint32_t rs, uint32_t rt, uint32_t rd, uint32_t)
{
    int32_t const signed_rs = core.gpr[rs];
    int32_t const signed_rt = core.gpr[rt];
    int32_t result;
    auto const overflow = ckd_sub(&result, signed_rs, signed_rt);
    if (overflow) {
        HWAlignmentCheck::disable();
        throw OverflowException{};
    }
    core.gpr[rd] = result;
}

template<R3000CoreConfig c>
void R3000Core<c>::Private::run_r_subu(uint32_t rs, uint32_t rt, uint32_t rd, uint32_t)
{
    core.gpr[rd] = core.gpr[rs] - core.gpr[rt];
}

template<R3000CoreConfig c>
void R3000Core<c>::Private::run_r_and(uint32_t rs, uint32_t rt, uint32_t rd, uint32_t)
{
    core.gpr[rd] = core.gpr[rs] & core.gpr[rt];
}

template<R3000CoreConfig c>
void R3000Core<c>::Private::run_r_or(uint32_t rs, uint32_t rt, uint32_t rd, uint32_t)
{
    core.gpr[rd] = core.gpr[rs] | core.gpr[rt];
}

template<R3000CoreConfig c>
void R3000Core<c>::Private::run_r_xor(uint32_t rs, uint32_t rt, uint32_t rd, uint32_t)
{
    core.gpr[rd] = core.gpr[rs] ^ core.gpr[rt];
}

template<R3000CoreConfig c>
void R3000Core<c>::Private::run_r_nor(uint32_t rs, uint32_t rt, uint32_t rd, uint32_t)
{
    core.gpr[rd] = ~(core.gpr[rs] | core.gpr[rt]);
}

template<R3000CoreConfig c>
void R3000Core<c>::Private::run_r_slt(uint32_t rs, uint32_t rt, uint32_t rd, uint32_t)
{
    int32_t const signed_rs = core.gpr[rs];
    int32_t const signed_rt = core.gpr[rt];
    core.gpr[rd] = signed_rs < signed_rt;
}

template<R3000CoreConfig c>
void R3000Core<c>::Private::run_r_sltu(uint32_t rs, uint32_t rt, uint32_t rd, uint32_t)
{
    core.gpr[rd] = core.gpr[rs] < core.gpr[rt];
}

template<R3000CoreConfig c>
void R3000Core<c>::Private::run_r_unk(uint32_t, uint32_t, uint32_t, uint32_t)
{
    throw InstructionException{};
}

template<R3000CoreConfig c>
void R3000Core<c>::Private::run_bcond_bltz(uint32_t rs, uint32_t offset, bool link)
{
    int32_t const signed_rs = core.gpr[rs];
    if (signed_rs < 0) {
        run_branch(offset);
    }
    if (link) {
        core.gpr[GPRName::ra] = core.pc + sizeof(uint32_t) * 2;
    }
}

template<R3000CoreConfig c>
void R3000Core<c>::Private::run_bcond_bgez(uint32_t rs, uint32_t offset, bool link)
{
    int32_t const signed_rs = core.gpr[rs];
    if (signed_rs >= 0) {
        run_branch(offset);
    }
    if (link) {
        core.gpr[GPRName::ra] = core.pc + sizeof(uint32_t) * 2;
    }
}

template<R3000CoreConfig c>
void R3000Core<c>::Private::run_ij_j(uint32_t, uint32_t, uint32_t, uint32_t target)
{
    auto const delay_slot_addr = core.pc + sizeof(uint32_t);
    auto const target_addr = (delay_slot_addr & 0xfc000000u) | (target << 2);
    set_branch_target(target_addr);
}

template<R3000CoreConfig c>
void R3000Core<c>::Private::run_ij_jal(uint32_t, uint32_t, uint32_t, uint32_t target)
{
    core.gpr[GPRName::ra] = core.pc + sizeof(uint32_t) * 2;
    auto const delay_slot_addr = core.pc + sizeof(uint32_t);
    auto const target_addr = (delay_slot_addr & 0xfc000000u) | (target << 2);
    set_branch_target(target_addr);
}

template<R3000CoreConfig c>
void R3000Core<c>::Private::run_ij_beq(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t)
{
    if (core.gpr[rs] == core.gpr[rt]) {
        run_branch(imm);
    }
}

template<R3000CoreConfig c>
void R3000Core<c>::Private::run_ij_bne(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t)
{
    if (core.gpr[rs] != core.gpr[rt]) {
        run_branch(imm);
    }
}

template<R3000CoreConfig c>
void R3000Core<c>::Private::run_ij_blez(uint32_t rs, uint32_t, uint32_t imm, uint32_t)
{
    int32_t const signed_rs = core.gpr[rs];
    if (signed_rs <= 0) {
        run_branch(imm);
    }
}

template<R3000CoreConfig c>
void R3000Core<c>::Private::run_ij_bgtz(uint32_t rs, uint32_t, uint32_t imm, uint32_t)
{
    int32_t const signed_rs = core.gpr[rs];
    if (signed_rs > 0) {
        run_branch(imm);
    }
}

template<R3000CoreConfig c>
void R3000Core<c>::Private::run_ij_addi(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t)
{
    int32_t const signed_rs = core.gpr[rs];
    int32_t const signed_imm = sign_extend_16(imm);
    int32_t result;
    auto const overflow = ckd_add(&result, signed_rs, signed_imm);
    if (overflow) {
        HWAlignmentCheck::disable();
        throw OverflowException{};
    }
    core.gpr[rt] = result;
}

template<R3000CoreConfig c>
void R3000Core<c>::Private::run_ij_addiu(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t)
{
    core.gpr[rt] = core.gpr[rs] + sign_extend_16(imm);
}

template<R3000CoreConfig c>
void R3000Core<c>::Private::run_ij_slti(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t)
{
    int32_t const signed_rs = core.gpr[rs];
    int32_t const signed_imm = sign_extend_16(imm);
    core.gpr[rt] = signed_rs < signed_imm;
}

template<R3000CoreConfig c>
void R3000Core<c>::Private::run_ij_sltiu(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t)
{
    core.gpr[rt] = core.gpr[rs] < sign_extend_16(imm);
}

template<R3000CoreConfig c>
void R3000Core<c>::Private::run_ij_andi(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t)
{
    core.gpr[rt] = core.gpr[rs] & imm;
}

template<R3000CoreConfig c>
void R3000Core<c>::Private::run_ij_ori(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t)
{
    core.gpr[rt] = core.gpr[rs] | imm;
}

template<R3000CoreConfig c>
void R3000Core<c>::Private::run_ij_xori(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t)
{
    core.gpr[rt] = core.gpr[rs] ^ imm;
}

template<R3000CoreConfig c>
void R3000Core<c>::Private::run_ij_lui(uint32_t, uint32_t rt, uint32_t imm, uint32_t)
{
    core.gpr[rt] = imm << 16;
}

template<R3000CoreConfig c>
void R3000Core<c>::Private::run_ij_lb(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t)
{
    load(rt, (int8_t)read_mem<uint8_t>(core.gpr[rs] + sign_extend_16(imm)));
}

template<R3000CoreConfig c>
void R3000Core<c>::Private::run_ij_lh(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t)
{
    load(rt, (int16_t)read_mem<uint16_t>(core.gpr[rs] + sign_extend_16(imm)));
}

template<R3000CoreConfig c>
void R3000Core<c>::Private::run_ij_lw(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t)
{
    load(rt, read_mem<uint32_t>(core.gpr[rs] + sign_extend_16(imm)));
}

template<R3000CoreConfig c>
void R3000Core<c>::Private::run_ij_lbu(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t)
{
    load(rt, read_mem<uint8_t>(core.gpr[rs] + sign_extend_16(imm)));
}

template<R3000CoreConfig c>
void R3000Core<c>::Private::run_ij_lhu(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t)
{
    load(rt, read_mem<uint16_t>(core.gpr[rs] + sign_extend_16(imm)));
}

template<R3000CoreConfig c>
void R3000Core<c>::Private::run_ij_lwl(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t)
{
    auto const prev_value =
        load_slot.enabled && load_slot.rt == rt ? load_slot.value : core.gpr[rt];
    auto const addr = core.gpr[rs] + sign_extend_16(imm);
    auto const misalignment = addr & 0x3;
    auto const aligned_addr = addr & ~0x3;
    auto const word = read_mem<uint32_t>(aligned_addr);
    auto const mask = 0x00ffffffu >> (misalignment * 8);
    auto const result = (prev_value & mask) | (word << ((3 - misalignment) * 8));
    load(rt, result);
}

template<R3000CoreConfig c>
void R3000Core<c>::Private::run_ij_lwr(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t)
{
    auto const prev_value =
        load_slot.enabled && load_slot.rt == rt ? load_slot.value : core.gpr[rt];
    auto const addr = core.gpr[rs] + sign_extend_16(imm);
    auto const misalignment = addr & 0x3;
    auto const aligned_addr = addr & ~0x3;
    auto const word = read_mem<uint32_t>(aligned_addr);
    auto const mask = 0xffffff00u << ((3 - misalignment) * 8);
    auto const result = (prev_value & mask) | (word >> (misalignment * 8));
    load(rt, result);
}

template<R3000CoreConfig c>
void R3000Core<c>::Private::run_ij_sb(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t)
{
    write_mem<uint8_t>(core.gpr[rs] + sign_extend_16(imm), core.gpr[rt]);
}

template<R3000CoreConfig c>
void R3000Core<c>::Private::run_ij_sh(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t)
{
    write_mem<uint16_t>(core.gpr[rs] + sign_extend_16(imm), core.gpr[rt]);
}

template<R3000CoreConfig c>
void R3000Core<c>::Private::run_ij_sw(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t)
{
    write_mem<uint32_t>(core.gpr[rs] + sign_extend_16(imm), core.gpr[rt]);
}

template<R3000CoreConfig c>
void R3000Core<c>::Private::run_ij_swl(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t)
{
    auto unaligned_ptr = core.gpr[rs] + sign_extend_16(imm);
    auto value = core.gpr[rt];
    do {
        write_mem<uint8_t>(unaligned_ptr, value >> 24);
        unaligned_ptr -= 1;
        value <<= 8;
    } while ((unaligned_ptr & 0x3) != 3);
}

template<R3000CoreConfig c>
void R3000Core<c>::Private::run_ij_swr(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t)
{
    auto unaligned_ptr = core.gpr[rs] + sign_extend_16(imm);
    auto value = core.gpr[rt];
    do {
        write_mem<uint8_t>(unaligned_ptr, value);
        unaligned_ptr += 1;
        value >>= 8;
    } while (unaligned_ptr & 0x3);
}

template<R3000CoreConfig c>
void R3000Core<c>::Private::run_ij_bios(uint32_t, uint32_t, uint32_t imm, uint32_t)
{
    load_slot_flush();
    auto const return_pc = core.gpr[GPRName::ra];
    HWAlignmentCheck::disable();
    bios.run_bios_fn(*parent, imm);
    HWAlignmentCheck::enable();
    // Substract 4, we are about to add 4 again.
    core.pc = return_pc - sizeof(r3000_ptr_t);
}

template<R3000CoreConfig c>
void R3000Core<c>::Private::run_ij_unk(uint32_t, uint32_t, uint32_t, uint32_t)
{
    throw InstructionException{};
}

template<R3000CoreConfig c>
R3000Core<c>::R3000Core()
    : p{std::make_unique<R3000Core<c>::Private>(this)}
{}

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
    p->core.gpr[GPRName::sp] = sp;
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
uint8_t* R3000Core<c>::get_buffer_checked(r3000_ptr_t addr, uint32_t size) const
{
    // This function only allows getting a buffer for RAM, at either of its mirror locations. For
    // this reason, the first check is that the size is no higher than 2MB.
    if (size > 0x200000u) {
        throw AddressException{addr + size};
    }

    // Now, check that the base address is within any of the RAM regions.
    if (auto const addr_prefix = addr & 0xffe00000u;
        !std::ranges::contains(std::array{0x0u, 0x80000000u, 0xa0000000u}, addr_prefix)) {
        throw AddressException{addr};
    }

    // Lastly, check that addr + size lies within the limits.
    if (auto const addr_suffix = addr & 0x1fffffu; addr_suffix + size > 0x200000u) {
        throw AddressException{addr + size};
    }

    return p->bus.get_mem_ptr() + addr;
}

// Explicit instantiation
template class R3000Core<{FaultCheck::HARDWARE, AlignmentCheck::HARDWARE}>;

std::unique_ptr<R3000> R3000::build()
{
    return std::make_unique<R3000Core<{FaultCheck::HARDWARE, AlignmentCheck::HARDWARE}>>();
}
