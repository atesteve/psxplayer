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

struct Core {
    std::array<uint32_t, 32> gpr{};
    uint32_t lo{};
    uint32_t hi{};
    r3000_ptr_t pc{};
};

struct AddressException {
    r3000_ptr_t addr{};
    AccessType rw{};
    AccessWidth access_width{};
};

struct OverflowException {};

struct InstructionException {
    uint16_t opcode{};
    uint16_t func_code{};
};

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

    operator bool() const {
        return _ptr;
    }

    bool operator==(std::nullptr_t) const {
        return _ptr == nullptr;
    }

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

struct GPRName {
    static constexpr size_t r0{0};
    static constexpr size_t at{1};
    static constexpr size_t v0{2};
    static constexpr size_t v1{3};
    static constexpr size_t a0{4};
    static constexpr size_t a1{5};
    static constexpr size_t a2{6};
    static constexpr size_t a3{7};
    static constexpr size_t t0{8};
    static constexpr size_t t1{9};
    static constexpr size_t t2{10};
    static constexpr size_t t3{11};
    static constexpr size_t t4{12};
    static constexpr size_t t5{13};
    static constexpr size_t t6{14};
    static constexpr size_t t7{15};
    static constexpr size_t s0{16};
    static constexpr size_t s1{17};
    static constexpr size_t s2{18};
    static constexpr size_t s3{19};
    static constexpr size_t s4{20};
    static constexpr size_t s5{21};
    static constexpr size_t s6{22};
    static constexpr size_t s7{23};
    static constexpr size_t t8{24};
    static constexpr size_t t9{25};
    static constexpr size_t k0{26};
    static constexpr size_t k1{27};
    static constexpr size_t gp{28};
    static constexpr size_t sp{29};
    static constexpr size_t s8{30};
    static constexpr size_t ra{31};
};

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

    template<typename T = uint8_t>
    EmuBuffer<T> get_buffer(uint32_t addr, uint32_t size)
    {
        auto* ptr = get_buffer_checked(addr, sizeof(T) * size);
        return EmuBuffer<T>{reinterpret_cast<T*>(ptr), addr, size};
    }

    template<typename T = uint8_t const>
    EmuBuffer<T> get_buffer(uint32_t addr, uint32_t size) const
    {
        auto* ptr = get_buffer_checked(addr, sizeof(T) * size);
        return EmuBuffer<T>{reinterpret_cast<T*>(ptr), addr, size};
    }

protected:
    virtual uint8_t read_mem_u8(r3000_ptr_t addr) const = 0;
    virtual uint16_t read_mem_u16(r3000_ptr_t addr) const = 0;
    virtual uint32_t read_mem_u32(r3000_ptr_t addr) const = 0;

    virtual void write_mem_u8(r3000_ptr_t addr, uint8_t value) = 0;
    virtual void write_mem_u16(r3000_ptr_t addr, uint16_t value) = 0;
    virtual void write_mem_u32(r3000_ptr_t addr, uint32_t value) = 0;

    virtual uint8_t* get_buffer_checked(r3000_ptr_t addr, uint32_t size) const = 0;
};
