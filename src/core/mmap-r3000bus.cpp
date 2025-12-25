#include "mmap-r3000bus.h"
#include "register-dispatcher.h"
#include "util/util.h"

#include <fmt/format.h>

#include <sys/mman.h>
#include <unistd.h>
#include <signal.h>
#include <ucontext.h>
#include <string.h>

#include <stdexcept>
#include <string_view>
#include <array>
#include <algorithm>
#include <csignal>

namespace {

// 4GB
constexpr uintptr_t MEMORY_SPACE_SIZE = 0x100000000ul;

constexpr uint32_t PSX_RAM_SIZE = 2 * 1024 * 1024;
constexpr auto PSX_RAM_ADDRS = std::to_array<r3000_ptr_t>({0x0, 0x80000000, 0xa0000000});

// It's 1024 bytes, not 4096, but we are limited by the page size.
constexpr uint32_t PSX_SPAD_SIZE = 4096;
constexpr auto PSX_SPAD_ADDRS = std::to_array<r3000_ptr_t>({0x1f800000, 0x9f800000});

constexpr uint32_t PSX_IO_SIZE = 8192;
constexpr auto PSX_IO_ADDRS = std::to_array<r3000_ptr_t>({0x1f801000, 0x9f801000, 0xbf801000});

// When debugging, since we are expecting SIGSEGV to occur during normal program execution, we
// disable SIGSEGV with `handle SIGSEGV nostop noprint pass` or similar. Unfortunately, that means
// that we will miss "real" SIGSEGV signals during debug. This function explicitly raises a SIGTRAP
// so that gdb will stop. On non-debug builds, it does nothing. It would be nice to use
// `std::breakpoint_if_debugging`, but no compiler supports it yet.
void breakpoint()
{
#ifndef NDEBUG
    std::raise(SIGTRAP);
#endif
}

void throw_errno(std::string_view msg)
{
    auto const* errordesc = strerrordesc_np(errno);
    throw std::runtime_error{fmt::format("{}: {}", msg, errordesc)};
}

void map_memory(std::string name,
                int& fd,
                uint8_t* mem_space,
                uint32_t size,
                auto const& offsets,
                int flags = PROT_READ | PROT_WRITE)
{
    fd = memfd_create(name.c_str(), 0);
    if (fd == -1) {
        throw_errno(fmt::format("Failed to create {} memfd", name));
    }

    if (ftruncate(fd, size) == -1) {
        throw_errno(fmt::format("Can't resize {} to {} bytes", name, size));
    }

    for (auto const offset : offsets) {
        auto* addr = mem_space + offset;
        if (mmap(addr, size, flags, MAP_SHARED | MAP_FIXED, fd, 0) == nullptr) {
            throw_errno(fmt::format("Failed to map {} to address {:#010x}", name, offset));
        }
    }
}

void unmap_memory(int fd, uint8_t* mem_space, uint32_t size, auto const& offsets)
{
    if (fd < 0) {
        return;
    }
    close(fd);
    for (auto const offset : offsets) {
        auto* addr = mem_space + offset;
        munmap(addr, size);
    }
}

void install_handler(int signal,
                     std::string_view signal_name,
                     void (*handler)(int, siginfo_t*, void*))
{
    struct sigaction sa{};
    sa.sa_sigaction = handler;
    sa.sa_flags = SA_SIGINFO;
    if (sigaction(signal, &sa, nullptr) == -1) {
        throw_errno(fmt::format("Failed to register signal handler for {}", signal_name));
    }
}

void uninstall_handler(int signal)
{
    struct sigaction sa{};
    sa.sa_handler = SIG_DFL;
    sigaction(signal, &sa, nullptr);
}

void disable_alignment_check()
{
    asm("pushf\n"
        "andl $~0x40000, (%%rsp)\n"
        "popf\n"
        :);
}

void emulate_ret(ucontext_t* ucontext)
{
    // This funcion is always called from a signal handler when it is already known that the fault
    // happened at one of the "supported" functions (read_mem_impl<...>, write_mem_impl<...>). Those
    // functions are implemented in assembler so we know that they are a mov followed by a ret. We
    // can change the return address of the signal handler by emulating the ret instruction, e.g.,
    // read the return address from the stack and return "manually" by incrementing RSP and setting
    // RIP to the return address. This has two benefits:
    //
    //  1. If we are returning normally, we can't just return to the mov instruction because it will
    //     fault again. We have emulated the mov instruction anyway, so we don't want to execute it
    //     again even if it didn't fault. We could return to the ret instruction, but that would
    //     require knowing the length of the mov instruction, which is not that difficult to do, but
    //     just emulating ret works just as well and independently of the length of mov.
    //
    //  2. If we are throwing, returning to the caller directly makes the C++ exception work
    //     magically accross the signal handler. read_mem_impl<...> and write_mem_impl<...> are C++
    //     functions with noexcept(false), so the return addresses are landing pads. Under these
    //     circumstances, the exception just works even when thrown from a signal handler.

    auto* const rsp_ptr = (uintptr_t const*)ucontext->uc_mcontext.gregs[REG_RSP];
    ucontext->uc_mcontext.gregs[REG_RIP] = *rsp_ptr;
    ucontext->uc_mcontext.gregs[REG_RSP] += sizeof(void*);
}

consteval int BIT(int n)
{
    return 1 << n;
}

// clang-format off
enum x86_pf_error_code {
    X86_PF_PROT  = BIT(0),
    X86_PF_WRITE = BIT(1),
    X86_PF_USER  = BIT(2),
    X86_PF_RSVD  = BIT(3),
    X86_PF_INSTR = BIT(4),
    X86_PF_PK    = BIT(5),
    X86_PF_SHSTK = BIT(6),
    X86_PF_SGX   = BIT(15),
    X86_PF_RMP   = BIT(31),
};
// clang-format on

} // namespace

struct MMAPR3000Bus::Private {
    static void static_sigsegv_handler(int, siginfo_t*, void*);
    static void static_sigbus_handler(int, siginfo_t*, void*);
    static inline MMAPR3000Bus::Private* signal_ptr;

    explicit Private(R3000* emu)
        : emu{emu}
    {}

    ~Private();

    // Use an initialization function instead of a constructor so that the destructor always runs.
    void init();

    template<std::integral Int>
    Int sigsegv_handler(void* ptr, AccessType type, Int value = 0);

    void sigsegv_handler(ucontext_t* ucp);
    void sigbus_handler(ucontext_t* ucp);

    void unprotect_hw(bool throwOnError = true);
    void protect_hw(bool throwOnError = true);

    uint8_t* mem_space = nullptr;
    uint8_t* exec_page = nullptr;
    int ram_memfd = -1;
    int spad_memfd = -1;
    int io_memfd = -1;
    R3000* emu;
    RegisterDispatcher dispatcher;
};

void MMAPR3000Bus::Private::init()
{
    if (signal_ptr) {
        throw std::runtime_error{"The signal handler is already installed!"};
    }

    signal_ptr = this;
    install_handler(SIGSEGV, "SIGSEGV", static_sigsegv_handler);
    install_handler(SIGBUS, "SIGBUS", static_sigbus_handler);

    mem_space =
        (uint8_t*)mmap(nullptr, MEMORY_SPACE_SIZE, PROT_NONE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    if (!mem_space) {
        throw_errno("Can't map 4GB memory block");
    }

    map_memory("psx-ram", ram_memfd, mem_space, PSX_RAM_SIZE, PSX_RAM_ADDRS);
    map_memory("psx-scratchpad", spad_memfd, mem_space, PSX_SPAD_SIZE, PSX_SPAD_ADDRS);
    map_memory("psx-io", io_memfd, mem_space, PSX_IO_SIZE, PSX_IO_ADDRS, PROT_NONE);

    exec_page = (uint8_t*)mmap(
        nullptr, 4096, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    if (!exec_page) {
        throw_errno("Can't map executable page");
    }

    dispatcher.set_mem_space(mem_space);
}

MMAPR3000Bus::Private::~Private()
{
    uninstall_handler(SIGSEGV);
    uninstall_handler(SIGBUS);

    unmap_memory(ram_memfd, mem_space, PSX_RAM_SIZE, PSX_RAM_ADDRS);
    unmap_memory(spad_memfd, mem_space, PSX_SPAD_SIZE, PSX_SPAD_ADDRS);
    unmap_memory(io_memfd, mem_space, PSX_IO_SIZE, PSX_IO_ADDRS);

    if (mem_space) {
        munmap(mem_space, MEMORY_SPACE_SIZE);
    }
    if (exec_page) {
        munmap(exec_page, 4096);
    }

    signal_ptr = nullptr;
}

void MMAPR3000Bus::Private::unprotect_hw(bool throwOnError)
{
    if (mprotect(mem_space + PSX_IO_ADDRS[0], PSX_IO_SIZE, PROT_READ | PROT_WRITE) == -1
        && throwOnError) {
        throw_errno("unprotect_hw");
    }
}

void MMAPR3000Bus::Private::protect_hw(bool throwOnError)
{
    if (mprotect(mem_space + PSX_IO_ADDRS[0], PSX_IO_SIZE, PROT_NONE) == -1 && throwOnError) {
        throw_errno("protect_hw");
    }
}

void MMAPR3000Bus::Private::static_sigsegv_handler(int, siginfo_t*, void* ucp)
{
    signal_ptr->sigsegv_handler((ucontext_t*)ucp);
}

void MMAPR3000Bus::Private::static_sigbus_handler(int, siginfo_t*, void* ucp)
{
    signal_ptr->sigbus_handler((ucontext_t*)ucp);
}

template<std::integral Int>
Int MMAPR3000Bus::Private::sigsegv_handler(void* ptr, AccessType type, Int value)
{
    disable_alignment_check();
    r3000_ptr_t const cpu_addr = (uint8_t*)ptr - mem_space;

    if (std::ranges::any_of(PSX_IO_ADDRS, [&](r3000_ptr_t hw_base_addr) {
            return cpu_addr >= hw_base_addr && cpu_addr + sizeof(Int) <= hw_base_addr + PSX_IO_SIZE;
        })) {
        // This is an address pointing to the lower mirror.
        auto const reg_addr = 0x1f800000u | (cpu_addr & (PSX_IO_SIZE - 1));

        // Open only the lower mirror. The handlers will use only that mirror.
        unprotect_hw();
        ScopeGuard reprotect{[&] {
            // Reprotect before returning.
            protect_hw(false);
        }};

        if (type == AccessType::WRITE) {
            dispatcher.write_reg<Int>(*emu, reg_addr, value);
            return 0;
        } else {
            return dispatcher.read_reg<Int>(*emu, reg_addr);
        }
    }

    auto const addr = (uint8_t*)ptr - mem_space;
    throw AddressException{(uint32_t)addr, type, int_width<Int>};
}

void MMAPR3000Bus::Private::sigsegv_handler(ucontext_t* ucontext)
{
    auto const reg_err = ucontext->uc_mcontext.gregs[REG_ERR];
    if ((reg_err & (X86_PF_USER | X86_PF_RSVD | X86_PF_INSTR | X86_PF_PK | X86_PF_SHSTK))
        != X86_PF_USER) {
        // Not the kind of fault we are expecting. Restore the default handler and return to let the
        // program crash.
        breakpoint();
        uninstall_handler(SIGSEGV);
        return;
    }

    bool const write_access = reg_err & X86_PF_WRITE;
    uintptr_t const rip = ucontext->uc_mcontext.gregs[REG_RIP];
    // The accessed addr is always in RDI if the fault happend at one of the supported instructions.
    void* const addr = (void*)ucontext->uc_mcontext.gregs[REG_RDI];

    try {
        if (write_access) {
            auto const value = ucontext->uc_mcontext.gregs[REG_RSI];
            if (rip == (uintptr_t)write_mem_impl<uint8_t>) {
                sigsegv_handler<uint8_t>(addr, AccessType::WRITE, value);
            } else if (rip == (uintptr_t)write_mem_impl<uint16_t>) {
                sigsegv_handler<uint16_t>(addr, AccessType::WRITE, value);
            } else if (rip == (uintptr_t)write_mem_impl<uint32_t>) {
                sigsegv_handler<uint32_t>(addr, AccessType::WRITE, value);
            } else {
                // Fault happened at an unknown instruction. Restore the default handler and return.
                breakpoint();
                uninstall_handler(SIGSEGV);
                return;
            }
        } else {
            auto& return_value = ucontext->uc_mcontext.gregs[REG_RAX];
            if (rip == (uintptr_t)read_mem_impl<uint8_t>) {
                return_value = sigsegv_handler<uint8_t>(addr, AccessType::READ);
            } else if (rip == (uintptr_t)read_mem_impl<uint16_t>) {
                return_value = sigsegv_handler<uint16_t>(addr, AccessType::READ);
            } else if (rip == (uintptr_t)read_mem_impl<uint32_t>) {
                return_value = sigsegv_handler<uint32_t>(addr, AccessType::READ);
            } else {
                // Fault happened at an unknown instruction. Restore the default handler and return.
                breakpoint();
                uninstall_handler(SIGSEGV);
                return;
            }
        }
        emulate_ret(ucontext);
    } catch (...) {
        // Disable alignment check before rethrowing. The C++ runtime generally doesn't respect
        // alignment.
        auto const eflags = ucontext->uc_mcontext.gregs[REG_EFL];
        ucontext->uc_mcontext.gregs[REG_EFL] = eflags & ~0x40000;
        emulate_ret(ucontext);
        throw;
    }
}

void MMAPR3000Bus::Private::sigbus_handler(ucontext_t* ucontext)
{
    disable_alignment_check();

    // Disable alignment check in the "calling" stack frame. We need to do this no matter what we
    // do, either by returning and recovering, or throwing a C++ exception.
    auto const eflags = ucontext->uc_mcontext.gregs[REG_EFL];
    ucontext->uc_mcontext.gregs[REG_EFL] = eflags & ~0x40000;

    auto const access = (ucontext->uc_mcontext.gregs[REG_ERR] & X86_PF_WRITE) ? AccessType::WRITE
                                                                              : AccessType::READ;
    uintptr_t const rip = ucontext->uc_mcontext.gregs[REG_RIP];
    AccessWidth width;

    if (rip == (uintptr_t)write_mem_impl<uint8_t> || rip == (uintptr_t)read_mem_impl<uint8_t>) {
        width = AccessWidth::A8;
    } else if (rip == (uintptr_t)write_mem_impl<uint16_t>
               || rip == (uintptr_t)read_mem_impl<uint16_t>) {
        width = AccessWidth::A16;
    } else if (rip == (uintptr_t)write_mem_impl<uint32_t>
               || rip == (uintptr_t)read_mem_impl<uint32_t>) {
        width = AccessWidth::A32;
    } else {
        // TODO: If we are here, it means we got an unaligned access outside of the "expected"
        // locations. Try to recover by returning to a copy of the faulting instruction followed by
        // re-enabling alignment check.
        return;
    }

    // Emulate the ret instruction so that the C++ exception propagates correctly.
    emulate_ret(ucontext);
    // Throw
    void* const addr = (void*)ucontext->uc_mcontext.gregs[REG_RDI];
    r3000_ptr_t const emu_addr = (uint8_t*)addr - mem_space;
    throw AddressException{emu_addr, access, width};
}

void MMAPR3000Bus::protect_hw() {
    _p->protect_hw();
}

void MMAPR3000Bus::unprotect_hw() {
    _p->unprotect_hw();
}

MMAPR3000Bus::MMAPR3000Bus(R3000* emu)
    : _p{std::make_unique<Private>(emu)}
{
    _p->init();
    _mem_space = _p->mem_space;
}

MMAPR3000Bus::~MMAPR3000Bus() = default;
