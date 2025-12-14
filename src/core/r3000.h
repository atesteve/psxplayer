#pragma once

#include <memory>
#include <stdexcept>

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
    A8, A16, A32,
};

struct R3000CoreConfig {
    FaultCheck fault_check{};
    AlignmentCheck alignment_check{};
};

template<R3000CoreConfig>
class R3000Core {
public:
    explicit R3000Core();
    ~R3000Core();

    uint8_t* get_mem_ptr();
    void set_regs(uint32_t sp, uint32_t pc);
    void run();

private:
    struct Private;
    std::unique_ptr<Private> p;
};

class R3000Exception : public std::logic_error {
public:
    using std::logic_error::logic_error;
};

using r3000_ptr_t = uint32_t;

struct MipsException {};

class AddressException : public MipsException {
public:
    explicit AddressException(r3000_ptr_t addr, AccessType rw, AccessWidth access_width)
        : addr{addr}
        , rw{rw}
        , access_width{access_width}
    {}

    r3000_ptr_t addr;
    AccessType rw;
    AccessWidth access_width;
};

class OverflowException : public MipsException {};
