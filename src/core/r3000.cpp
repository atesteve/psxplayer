#include "r3000.h"

#include <fmt/format.h>

#include <array>
#include <cstdint>
#include <concepts>
#include <string_view>
#include <bit>
#include <utility>
#include <stdckdint.h>
#include <optional>

using namespace fmt::literals;

namespace {

template<std::integral>
constexpr std::string_view int_name;

// clang-format off
template<> [[maybe_unused]] constexpr std::string_view int_name<int8_t>   = "i8";
template<> [[maybe_unused]] constexpr std::string_view int_name<uint8_t>  = "u8";
template<> [[maybe_unused]] constexpr std::string_view int_name<int16_t>  = "i16";
template<> [[maybe_unused]] constexpr std::string_view int_name<uint16_t> = "u16";
template<> [[maybe_unused]] constexpr std::string_view int_name<int32_t>  = "i32";
template<> [[maybe_unused]] constexpr std::string_view int_name<uint32_t> = "u32";
// clang-format on

using r3000_ptr_t = uint32_t;

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

uint32_t sign_extend_8(uint32_t x)
{
    return (int8_t)x;
}

} // namespace

class AddressException : public R3000Exception {
public:
    explicit AddressException(r3000_ptr_t addr, AccessType rw, std::string_view access_width)
        : R3000Exception{fmt::format("Misaligned {} {} access at address {:08x}",
                                     access_width,
                                     rw == AccessType::READ ? "read" : "write",
                                     addr)}
        , addr{addr}
        , rw{rw}
        , access_width{access_width}
    {}

    r3000_ptr_t addr;
    AccessType rw;
    std::string_view access_width;
};

class OverflowException : public R3000Exception {
public:
    explicit OverflowException(std::string_view operation, int32_t a, int32_t b)
        : R3000Exception{fmt::format("Integer overflow {} on operands {}, {}", operation, a, b)}
        , operation{operation}
        , a{a}
        , b{b}
    {}

    std::string_view operation;
    int32_t a;
    int32_t b;
};

template<R3000CoreConfig c>
struct R3000Core<c>::Private {

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

    void run();

    void run_r_inst(uint32_t function, uint32_t rs, uint32_t rt, uint32_t rd, uint32_t shift);
    void run_ij_inst(uint32_t opcode, uint32_t rs, uint32_t rt, uint32_t imm, uint32_t target);

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
    void run_ij_lwl(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t target);
    void run_ij_lw(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t target);
    void run_ij_lbu(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t target);
    void run_ij_lhu(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t target);
    void run_ij_lwr(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t target);
    void run_ij_sb(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t target);
    void run_ij_sh(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t target);
    void run_ij_swl(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t target);
    void run_ij_sw(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t target);
    void run_ij_swr(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t target);
    void run_ij_lwc0(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t target);
    void run_ij_lwc1(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t target);
    void run_ij_lwc2(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t target);
    void run_ij_lwc3(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t target);
    void run_ij_swc0(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t target);
    void run_ij_swc1(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t target);
    void run_ij_swc2(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t target);
    void run_ij_swc3(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t target);
    void run_ij_unk(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t target);

    template<std::integral Int>
    void check_alignment(r3000_ptr_t addr, AccessType rw) const
    {
        if constexpr (c.alignment_check == AlignmentCheck::SOFTWARE) {
            static constexpr r3000_ptr_t mask = (1 << sizeof(Int)) - 1;
            if (addr & mask) {
                throw AddressException{addr, rw, int_name<Int>};
            }
        }
        // Do nothing otherwise.
    }

    template<std::integral Int>
    Int read_mem(r3000_ptr_t addr) const
    {
        check_alignment<Int>(addr, AccessType::READ);
        return 0;
    }

    template<std::integral Int>
    void write_mem(r3000_ptr_t addr, Int value) const
    {
        check_alignment<Int>(addr, AccessType::WRITE);
    }

    std::array<uint32_t, 32> gpr{};
    uint32_t lo{};
    uint32_t hi{};
    r3000_ptr_t pc{};

    struct load_delay_slot {
        R3000Core<c>::Private* self;
        uint32_t rt;
        uint32_t value;

        ~load_delay_slot() {
            self->gpr[rt] = value;
        }
    };

    std::optional<load_delay_slot> load_slot;
};

template<R3000CoreConfig c>
void R3000Core<c>::Private::run()
{
    HWAlignmentCheckGuard align_check_guard{};

    auto const raw_inst = read_mem<r3000_ptr_t>(pc);
    auto const opcode = raw_inst >> 26;

    if (opcode == 0) {
        auto const [function, shift, rd, rt, rs, _] = std::bit_cast<reg_inst_t>(raw_inst);
        run_r_inst(function, rs, rt, rd, shift);
    } else {
        auto const [imm, rt, rs, _] = std::bit_cast<imm_inst_t>(raw_inst);
        auto const [target, _] = std::bit_cast<jump_inst_t>(raw_inst);
        run_ij_inst(opcode, rs, rt, imm, target);
    }
    // After execution of the instruction, set R0 back to 0 in case it was written.
    gpr[0] = 0;
}

template<R3000CoreConfig c>
void R3000Core<c>::Private::run_r_inst(uint32_t function,
                                       uint32_t rs,
                                       uint32_t rt,
                                       uint32_t rd,
                                       uint32_t shift)
{
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
}

template<R3000CoreConfig c>
void R3000Core<c>::Private::run_ij_inst(uint32_t opcode,
                                        uint32_t rs,
                                        uint32_t rt,
                                        uint32_t imm,
                                        uint32_t target)
{
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
        case 0x30: return run_ij_lwc0 (rs, rt, imm, target);
        case 0x31: return run_ij_lwc1 (rs, rt, imm, target);
        case 0x32: return run_ij_lwc2 (rs, rt, imm, target);
        case 0x33: return run_ij_lwc3 (rs, rt, imm, target);
        case 0x34: return run_ij_unk  (rs, rt, imm, target);
        case 0x35: return run_ij_unk  (rs, rt, imm, target);
        case 0x36: return run_ij_unk  (rs, rt, imm, target);
        case 0x37: return run_ij_unk  (rs, rt, imm, target);
        case 0x38: return run_ij_swc0 (rs, rt, imm, target);
        case 0x39: return run_ij_swc1 (rs, rt, imm, target);
        case 0x3a: return run_ij_swc2 (rs, rt, imm, target);
        case 0x3b: return run_ij_swc3 (rs, rt, imm, target);
        case 0x3c: return run_ij_unk  (rs, rt, imm, target);
        case 0x3d: return run_ij_unk  (rs, rt, imm, target);
        case 0x3e: return run_ij_unk  (rs, rt, imm, target);
        case 0x3f: return run_ij_unk  (rs, rt, imm, target);
        // `opcode` is 6 bit wide, so it's impossible to receive anything higher than 0x3f (63).
        default: return std::unreachable();
    };
    // clang-format on
}

template<R3000CoreConfig c>
void R3000Core<c>::Private::run_r_sll(uint32_t, uint32_t rt, uint32_t rd, uint32_t shift)
{
    gpr[rd] = gpr[rt] << shift;
}

template<R3000CoreConfig c>
void R3000Core<c>::Private::run_r_srl(uint32_t, uint32_t rt, uint32_t rd, uint32_t shift)
{
    gpr[rd] = gpr[rt] >> shift;
}

template<R3000CoreConfig c>
void R3000Core<c>::Private::run_r_sra(uint32_t, uint32_t rt, uint32_t rd, uint32_t shift)
{
    int32_t const signed_rt = gpr[rt];
    gpr[rd] = signed_rt >> shift;
}

template<R3000CoreConfig c>
void R3000Core<c>::Private::run_r_sllv(uint32_t rs, uint32_t rt, uint32_t rd, uint32_t)
{
    gpr[rd] = gpr[rt] << (gpr[rs] & 0x1f);
}

template<R3000CoreConfig c>
void R3000Core<c>::Private::run_r_srlv(uint32_t rs, uint32_t rt, uint32_t rd, uint32_t)
{
    gpr[rd] = gpr[rt] >> (gpr[rs] & 0x1f);
}

template<R3000CoreConfig c>
void R3000Core<c>::Private::run_r_srav(uint32_t rs, uint32_t rt, uint32_t rd, uint32_t)
{
    int32_t const signed_rt = gpr[rt];
    gpr[rd] = signed_rt >> (gpr[rs] & 0x1f);
}

template<R3000CoreConfig c>
void R3000Core<c>::Private::run_r_jr(uint32_t rs, uint32_t rt, uint32_t rd, uint32_t shift)
{}

template<R3000CoreConfig c>
void R3000Core<c>::Private::run_r_jalr(uint32_t rs, uint32_t rt, uint32_t rd, uint32_t shift)
{}

template<R3000CoreConfig c>
void R3000Core<c>::Private::run_r_syscall(uint32_t rs, uint32_t rt, uint32_t rd, uint32_t shift)
{}

template<R3000CoreConfig c>
void R3000Core<c>::Private::run_r_break(uint32_t rs, uint32_t rt, uint32_t rd, uint32_t shift)
{}

template<R3000CoreConfig c>
void R3000Core<c>::Private::run_r_mfhi(uint32_t, uint32_t, uint32_t rd, uint32_t)
{
    gpr[rd] = hi;
}

template<R3000CoreConfig c>
void R3000Core<c>::Private::run_r_mthi(uint32_t rs, uint32_t, uint32_t, uint32_t)
{
    hi = gpr[rs];
}

template<R3000CoreConfig c>
void R3000Core<c>::Private::run_r_mflo(uint32_t, uint32_t, uint32_t rd, uint32_t)
{
    gpr[rd] = lo;
}

template<R3000CoreConfig c>
void R3000Core<c>::Private::run_r_mtlo(uint32_t rs, uint32_t, uint32_t, uint32_t)
{
    lo = gpr[rs];
}

template<R3000CoreConfig c>
void R3000Core<c>::Private::run_r_mult(uint32_t rs, uint32_t rt, uint32_t, uint32_t)
{
    int32_t const signed_rs = gpr[rs];
    int32_t const signed_rt = gpr[rt];
    uint64_t const result = (int64_t)signed_rs * signed_rt;
    lo = result;
    hi = result >> 32;
}

template<R3000CoreConfig c>
void R3000Core<c>::Private::run_r_multu(uint32_t rs, uint32_t rt, uint32_t rd, uint32_t shift)
{
    uint64_t const result = (uint64_t)gpr[rs] * gpr[rt];
    lo = result;
    hi = result >> 32;
}

template<R3000CoreConfig c>
void R3000Core<c>::Private::run_r_div(uint32_t rs, uint32_t rt, uint32_t, uint32_t)
{
    int32_t const signed_rs = gpr[rs];
    int32_t const signed_rt = gpr[rt];
    if (signed_rt == 0) {
        lo = signed_rs > 0 ? -1 : 1;
        hi = signed_rs;
    } else if (signed_rs == std::numeric_limits<int32_t>::min() && signed_rt == -1) {
        lo = std::numeric_limits<int32_t>::min();
        hi = 0;
    } else {
        lo = signed_rs / signed_rt;
        hi = signed_rs % signed_rt;
    }
}

template<R3000CoreConfig c>
void R3000Core<c>::Private::run_r_divu(uint32_t rs, uint32_t rt, uint32_t, uint32_t)
{
    if (gpr[rt] == 0) {
        lo = 0xffffffff;
        hi = gpr[rs];
    } else {
        lo = gpr[rs] / gpr[rt];
        hi = gpr[rs] % gpr[rt];
    }
}

template<R3000CoreConfig c>
void R3000Core<c>::Private::run_r_add(uint32_t rs, uint32_t rt, uint32_t rd, uint32_t)
{
    int32_t const signed_rs = gpr[rs];
    int32_t const signed_rt = gpr[rt];
    int32_t result;
    auto const overflow = ckd_add(&result, signed_rs, signed_rt);
    if (overflow) {
        throw OverflowException{"add", signed_rs, signed_rt};
    }
    gpr[rd] = result;
}

template<R3000CoreConfig c>
void R3000Core<c>::Private::run_r_addu(uint32_t rs, uint32_t rt, uint32_t rd, uint32_t)
{
    gpr[rd] = gpr[rs] + gpr[rt];
}

template<R3000CoreConfig c>
void R3000Core<c>::Private::run_r_sub(uint32_t rs, uint32_t rt, uint32_t rd, uint32_t)
{
    int32_t const signed_rs = gpr[rs];
    int32_t const signed_rt = gpr[rt];
    int32_t result;
    auto const overflow = ckd_sub(&result, signed_rs, signed_rt);
    if (overflow) {
        throw OverflowException{"sub", signed_rs, signed_rt};
    }
    gpr[rd] = result;
}

template<R3000CoreConfig c>
void R3000Core<c>::Private::run_r_subu(uint32_t rs, uint32_t rt, uint32_t rd, uint32_t)
{
    gpr[rd] = gpr[rs] - gpr[rt];
}

template<R3000CoreConfig c>
void R3000Core<c>::Private::run_r_and(uint32_t rs, uint32_t rt, uint32_t rd, uint32_t)
{
    gpr[rd] = gpr[rs] & gpr[rt];
}

template<R3000CoreConfig c>
void R3000Core<c>::Private::run_r_or(uint32_t rs, uint32_t rt, uint32_t rd, uint32_t)
{
    gpr[rd] = gpr[rs] | gpr[rt];
}

template<R3000CoreConfig c>
void R3000Core<c>::Private::run_r_xor(uint32_t rs, uint32_t rt, uint32_t rd, uint32_t)
{
    gpr[rd] = gpr[rs] ^ gpr[rt];
}

template<R3000CoreConfig c>
void R3000Core<c>::Private::run_r_nor(uint32_t rs, uint32_t rt, uint32_t rd, uint32_t)
{
    gpr[rd] = ~(gpr[rs] | gpr[rt]);
}

template<R3000CoreConfig c>
void R3000Core<c>::Private::run_r_slt(uint32_t rs, uint32_t rt, uint32_t rd, uint32_t)
{
    int32_t const signed_rs = gpr[rs];
    int32_t const signed_rt = gpr[rt];
    gpr[rd] = signed_rs < signed_rt;
}

template<R3000CoreConfig c>
void R3000Core<c>::Private::run_r_sltu(uint32_t rs, uint32_t rt, uint32_t rd, uint32_t)
{
    gpr[rd] = gpr[rs] < gpr[rt];
}

template<R3000CoreConfig c>
void R3000Core<c>::Private::run_r_unk(uint32_t rs, uint32_t rt, uint32_t rd, uint32_t shift)
{}

template<R3000CoreConfig c>
void R3000Core<c>::Private::run_ij_j(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t target)
{}

template<R3000CoreConfig c>
void R3000Core<c>::Private::run_ij_jal(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t target)
{}

template<R3000CoreConfig c>
void R3000Core<c>::Private::run_ij_beq(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t target)
{}

template<R3000CoreConfig c>
void R3000Core<c>::Private::run_ij_bne(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t target)
{}

template<R3000CoreConfig c>
void R3000Core<c>::Private::run_ij_blez(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t target)
{}

template<R3000CoreConfig c>
void R3000Core<c>::Private::run_ij_bgtz(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t target)
{}

template<R3000CoreConfig c>
void R3000Core<c>::Private::run_ij_addi(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t)
{
    int32_t const signed_rs = gpr[rs];
    int32_t const signed_imm = sign_extend_16(imm);
    int32_t result;
    auto const overflow = ckd_add(&result, signed_rs, signed_imm);
    if (overflow) {
        throw OverflowException{"addi", signed_rs, signed_imm};
    }
    gpr[rt] = result;
}

template<R3000CoreConfig c>
void R3000Core<c>::Private::run_ij_addiu(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t)
{
    gpr[rt] = gpr[rs] + sign_extend_16(imm);
}

template<R3000CoreConfig c>
void R3000Core<c>::Private::run_ij_slti(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t)
{
    int32_t const signed_rs = gpr[rs];
    int32_t const signed_imm = sign_extend_16(imm);
    gpr[rt] = signed_rs < signed_imm;
}

template<R3000CoreConfig c>
void R3000Core<c>::Private::run_ij_sltiu(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t)
{
    gpr[rt] = gpr[rs] < sign_extend_16(imm);
}

template<R3000CoreConfig c>
void R3000Core<c>::Private::run_ij_andi(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t)
{
    gpr[rt] = gpr[rs] & imm;
}

template<R3000CoreConfig c>
void R3000Core<c>::Private::run_ij_ori(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t)
{
    gpr[rt] = gpr[rs] | imm;
}

template<R3000CoreConfig c>
void R3000Core<c>::Private::run_ij_xori(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t)
{
    gpr[rt] = gpr[rs] ^ imm;
}

template<R3000CoreConfig c>
void R3000Core<c>::Private::run_ij_lui(uint32_t, uint32_t rt, uint32_t imm, uint32_t)
{
    gpr[rt] = imm << 16;
}

template<R3000CoreConfig c>
void R3000Core<c>::Private::run_ij_lb(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t)
{
    load_slot.emplace(this, rt, read_mem<int8_t>(rs + sign_extend_16(imm)));
}

template<R3000CoreConfig c>
void R3000Core<c>::Private::run_ij_lh(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t)
{
    load_slot.emplace(this, rt, read_mem<int16_t>(rs + sign_extend_16(imm)));
}

template<R3000CoreConfig c>
void R3000Core<c>::Private::run_ij_lwl(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t target)
{}

template<R3000CoreConfig c>
void R3000Core<c>::Private::run_ij_lw(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t)
{
    load_slot.emplace(this, rt, read_mem<uint32_t>(rs + sign_extend_16(imm)));
}

template<R3000CoreConfig c>
void R3000Core<c>::Private::run_ij_lbu(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t)
{
    load_slot.emplace(this, rt, read_mem<uint8_t>(rs + sign_extend_16(imm)));
}

template<R3000CoreConfig c>
void R3000Core<c>::Private::run_ij_lhu(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t)
{
    load_slot.emplace(this, rt, read_mem<uint16_t>(rs + sign_extend_16(imm)));
}

template<R3000CoreConfig c>
void R3000Core<c>::Private::run_ij_lwr(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t target)
{}

template<R3000CoreConfig c>
void R3000Core<c>::Private::run_ij_sb(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t target)
{}

template<R3000CoreConfig c>
void R3000Core<c>::Private::run_ij_sh(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t target)
{}

template<R3000CoreConfig c>
void R3000Core<c>::Private::run_ij_swl(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t target)
{}

template<R3000CoreConfig c>
void R3000Core<c>::Private::run_ij_sw(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t target)
{}

template<R3000CoreConfig c>
void R3000Core<c>::Private::run_ij_swr(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t target)
{}

template<R3000CoreConfig c>
void R3000Core<c>::Private::run_ij_lwc0(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t target)
{}

template<R3000CoreConfig c>
void R3000Core<c>::Private::run_ij_lwc1(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t target)
{}

template<R3000CoreConfig c>
void R3000Core<c>::Private::run_ij_lwc2(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t target)
{}

template<R3000CoreConfig c>
void R3000Core<c>::Private::run_ij_lwc3(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t target)
{}

template<R3000CoreConfig c>
void R3000Core<c>::Private::run_ij_swc0(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t target)
{}

template<R3000CoreConfig c>
void R3000Core<c>::Private::run_ij_swc1(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t target)
{}

template<R3000CoreConfig c>
void R3000Core<c>::Private::run_ij_swc2(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t target)
{}

template<R3000CoreConfig c>
void R3000Core<c>::Private::run_ij_swc3(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t target)
{}

template<R3000CoreConfig c>
void R3000Core<c>::Private::run_ij_unk(uint32_t rs, uint32_t rt, uint32_t imm, uint32_t target)
{}

template<R3000CoreConfig c>
R3000Core<c>::R3000Core()
    : p{std::make_unique<R3000Core<c>::Private>()}
{}

template<R3000CoreConfig c>
R3000Core<c>::~R3000Core() = default;

template<R3000CoreConfig c>
void R3000Core<c>::run()
{
    p->run();
}

// Explicit instantiation
template class R3000Core<{}>;
template class R3000Core<{FaultCheck::SOFTWARE, AlignmentCheck::SOFTWARE}>;
template class R3000Core<{FaultCheck::HARDWARE, AlignmentCheck::HARDWARE}>;
