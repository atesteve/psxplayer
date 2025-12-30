#pragma once

#include <cstdint>
#include <array>
#include <memory>
#include <concepts>

enum class FaultCheck {
    SOFTWARE,
    HARDWARE,
};

enum class AlignmentCheck {
    NO_CHECK,
    SOFTWARE,
    HARDWARE,
};

enum class AccessType {
    READ,
    WRITE,
};

enum class AccessWidth {
    A8,
    A16,
    A32,
};

template<std::integral>
inline constexpr std::string_view int_name;

// clang-format off
template<> inline constexpr std::string_view int_name<int8_t>   = "i8";
template<> inline constexpr std::string_view int_name<uint8_t>  = "u8";
template<> inline constexpr std::string_view int_name<int16_t>  = "i16";
template<> inline constexpr std::string_view int_name<uint16_t> = "u16";
template<> inline constexpr std::string_view int_name<int32_t>  = "i32";
template<> inline constexpr std::string_view int_name<uint32_t> = "u32";
// clang-format on

template<std::integral>
constexpr AccessWidth int_width{};

// clang-format off
template<> inline constexpr auto int_width<int8_t>   = AccessWidth::A8;
template<> inline constexpr auto int_width<uint8_t>  = AccessWidth::A8;
template<> inline constexpr auto int_width<int16_t>  = AccessWidth::A16;
template<> inline constexpr auto int_width<uint16_t> = AccessWidth::A16;
template<> inline constexpr auto int_width<int32_t>  = AccessWidth::A32;
template<> inline constexpr auto int_width<uint32_t> = AccessWidth::A32;
// clang-format on

using r3000_ptr_t = uint32_t;

struct GPR {
    uint32_t r0;
    uint32_t at;
    uint32_t v0;
    uint32_t v1;
    uint32_t a0;
    uint32_t a1;
    uint32_t a2;
    uint32_t a3;
    uint32_t t0;
    uint32_t t1;
    uint32_t t2;
    uint32_t t3;
    uint32_t t4;
    uint32_t t5;
    uint32_t t6;
    uint32_t t7;
    uint32_t s0;
    uint32_t s1;
    uint32_t s2;
    uint32_t s3;
    uint32_t s4;
    uint32_t s5;
    uint32_t s6;
    uint32_t s7;
    uint32_t t8;
    uint32_t t9;
    uint32_t k0;
    uint32_t k1;
    uint32_t gp;
    uint32_t sp;
    uint32_t s8;
    uint32_t ra;
};

struct CP0R {
    uint32_t Index;
    uint32_t Random;
    uint32_t EntryLo0;
    uint32_t BPC;
    uint32_t Context;
    uint32_t BDA;
    uint32_t PIDMask;
    uint32_t DCIC;
    uint32_t BadVAddr;
    uint32_t BDAM;
    uint32_t EntryHi;
    uint32_t BPCM;
    uint32_t Status;
    uint32_t Cause;
    uint32_t EPC;
    uint32_t PRid;
    uint32_t Config;
    uint32_t LLAddr;
    uint32_t WatchLO;
    uint32_t WatchHI;
    uint32_t XContext;
    uint32_t ECC;
    uint32_t CacheErr;
    uint32_t TagLo;
    uint32_t TagHi;
    uint32_t ErrorEPC;
};

struct Core {
    union {
        std::array<uint32_t, 32> r{};
        GPR n;
    } gpr{};
    uint32_t lo{};
    uint32_t hi{};
    r3000_ptr_t pc{};
};

struct CoreException {
    virtual ~CoreException() = default;
};

struct AddressException : public CoreException {
    explicit AddressException() = default;

    explicit AddressException(r3000_ptr_t addr)
        : addr{addr}
    {}

    explicit AddressException(r3000_ptr_t addr, AccessType rw, AccessWidth access_width)
        : addr{addr}
        , rw{rw}
        , access_width{access_width}
    {}

    r3000_ptr_t addr{};
    AccessType rw{};
    AccessWidth access_width{};
};

struct OverflowException : public CoreException {};

struct InstructionException : public CoreException {
    explicit InstructionException() = default;
    explicit InstructionException(uint16_t opcode, uint16_t func_code)
        : opcode{opcode}
        , func_code{func_code}
    {}

    uint16_t opcode{};
    uint16_t func_code{};
};

struct CoprocessorUnusableException : public CoreException {};

template<typename T>
class EmuBuffer {
public:
    using type = T;

    explicit EmuBuffer()
        : _ptr{nullptr}
        , _base_addr{0}
        , _size{0}
    {}

    explicit EmuBuffer(T* ptr, r3000_ptr_t base_addr, uint32_t size)
        : _ptr{ptr}
        , _base_addr{base_addr}
        , _size{size}
    {}

    EmuBuffer(EmuBuffer const&) = default;
    EmuBuffer& operator=(EmuBuffer const&) = default;

    T& operator[](uint32_t offset)
    {
        if (offset >= _size) {
            r3000_ptr_t const offset_bytes = offset * sizeof(T);
            throw AddressException{
                _base_addr + offset_bytes, AccessType::READ, int_width<std::remove_cv_t<T>>};
        }
        return _ptr[offset];
    }

    T const& operator[](uint32_t offset) const { return const_cast<EmuBuffer<T>*>(this)[offset]; }

    T& operator*() { return *_ptr; }

    T const& operator*() const { return *_ptr; }

    T* operator->() { return _ptr; }

    T const* operator->() const { return _ptr; }

    operator bool() const { return _ptr; }

    bool operator==(std::nullptr_t) const { return _ptr == nullptr; }

    T* data() { return _ptr; }

    T const* data() const { return _ptr; }

    uint32_t size() const { return _size; }
    uint32_t size_bytes() const { return _size * sizeof(T); }

    r3000_ptr_t base_addr() const { return _base_addr; }

private:
    T* _ptr;
    r3000_ptr_t _base_addr;
    uint32_t _size;
};

// clang-format off
struct HWReg {
    static constexpr size_t ISTAT = 0x1f801070;
    static constexpr size_t IMASK = 0x1f801074;
    static constexpr size_t DEVICE_BASE = 0x1f801000;
    static constexpr size_t DMA_start = 0x1f801080;
    static constexpr size_t DMA_end = 0x1f801100;
    static constexpr size_t SPU_start = 0x1f801c00;
    static constexpr size_t SPU_end = 0x1F802000;
};

struct IRQ {
    static constexpr size_t VBLANK     = 0;
    static constexpr size_t GPU        = 1;
    static constexpr size_t CDROM      = 2;
    static constexpr size_t DMA        = 3;
    static constexpr size_t TMR0       = 4;
    static constexpr size_t TMR1       = 5;
    static constexpr size_t TMR2       = 6;
    static constexpr size_t CONTROLLER = 7;
    static constexpr size_t SIO        = 8;
    static constexpr size_t SPU        = 9;
    static constexpr size_t PIO        = 10;
};

// clang-format on

struct R3000 {
    virtual ~R3000() = default;

    static std::unique_ptr<R3000> build();

    virtual void set_regs(uint32_t sp, uint32_t pc) = 0;
    virtual void run() = 0;

    template<std::integral Int>
    Int read_mem(r3000_ptr_t addr) const
    {
        if constexpr (std::is_same_v<Int, uint8_t>) {
            return read_mem_u8(addr);
        } else if constexpr (std::is_same_v<Int, uint16_t>) {
            return read_mem_u16(addr);
        } else if constexpr (std::is_same_v<Int, uint32_t>) {
            return read_mem_u32(addr);
        }
    }

    template<std::integral Int>
    void write_mem(r3000_ptr_t addr, Int value)
    {
        if constexpr (std::is_same_v<Int, uint8_t>) {
            write_mem_u8(addr, value);
        } else if constexpr (std::is_same_v<Int, uint16_t>) {
            write_mem_u16(addr, value);
        } else if constexpr (std::is_same_v<Int, uint32_t>) {
            write_mem_u32(addr, value);
        }
    }

    virtual Core& core() = 0;
    virtual Core const& core() const = 0;

    /**
     * Get a checked buffer of type T, at a given addr and with a given number of elements (size).
     * The buffer boundaries are checked, and throws AddressException if it lies outside of RAM
     * boundaries.
     */
    template<typename T = uint8_t, typename Self>
    auto get_buffer(this Self&& self, uint32_t addr, uint32_t size = 1)
    {
        using RetT = std::
            conditional_t<std::is_const_v<std::remove_reference_t<Self>>, std::add_const_t<T>, T>;
        auto* ptr = self.get_buffer_checked(addr, sizeof(T) * size, true);
        return EmuBuffer<RetT>{reinterpret_cast<RetT*>(ptr), addr, size};
    }

    template<typename T = uint8_t, typename Self>
    auto get_device_buffer(this Self&& self, uint32_t addr, uint32_t size = 1)
    {
        using RetT = std::
            conditional_t<std::is_const_v<std::remove_reference_t<Self>>, std::add_const_t<T>, T>;
        auto* ptr = self.get_buffer_checked(addr, sizeof(T) * size, false);
        return EmuBuffer<RetT>{reinterpret_cast<RetT*>(ptr), addr, size};
    }

    virtual uint32_t& istat() = 0;
    virtual uint32_t& imask() = 0;

    virtual void return_from_exception() = 0;

    virtual void write_dma_reg(r3000_ptr_t addr, uint32_t value) = 0;
    virtual uint32_t read_dma_reg(r3000_ptr_t addr) = 0;

    virtual void write_spu_reg(r3000_ptr_t addr, uint16_t value) = 0;
    virtual uint16_t read_spu_reg(r3000_ptr_t addr) = 0;

protected:
    virtual uint8_t read_mem_u8(r3000_ptr_t addr) const = 0;
    virtual uint16_t read_mem_u16(r3000_ptr_t addr) const = 0;
    virtual uint32_t read_mem_u32(r3000_ptr_t addr) const = 0;

    virtual void write_mem_u8(r3000_ptr_t addr, uint8_t value) = 0;
    virtual void write_mem_u16(r3000_ptr_t addr, uint16_t value) = 0;
    virtual void write_mem_u32(r3000_ptr_t addr, uint32_t value) = 0;

    virtual uint8_t* get_buffer_checked(r3000_ptr_t addr, uint32_t size, bool ram) const = 0;
};
