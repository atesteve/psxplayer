#include "mmap-r3000bus.h"
#include "r3000.h"

#include <fmt/format.h>

#include <sys/mman.h>
#include <unistd.h>
#include <signal.h>
#include <ucontext.h>
#include <string.h>

#include <stdexcept>
#include <string_view>
#include <array>
#include <exception>

namespace {

// 4GB
constexpr uintptr_t MEMORY_SPACE_SIZE = 0x100000000ul;

constexpr uint32_t PSX_RAM_SIZE = 2 * 1024 * 1024;
constexpr auto PSX_RAM_ADDRS = std::to_array<uint32_t>({0x0, 0x80000000, 0xa0000000});

// It's 1024 bytes, not 4096, but we are limited by the page size.
constexpr uint32_t PSX_SPAD_SIZE = 4096;
constexpr auto PSX_SPAD_ADDRS = std::to_array<uint32_t>({0x1f800000, 0x9f800000});

void throw_errno(std::string_view msg)
{
    auto const* errordesc = strerrordesc_np(errno);
    throw std::runtime_error{fmt::format("{}: {}", msg, errordesc)};
}

void map_memory(std::string name, int& fd, uint8_t* mem_space, uint32_t size, auto const& offsets)
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
        if (mmap(addr, size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, fd, 0) == nullptr) {
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
    static void static_sigsegv_hanlder(int, siginfo_t*, void*);
    static void static_sigbus_hanlder(int, siginfo_t*, void*);
    static void throw_from_handler();
    static inline MMAPR3000Bus::Private* signal_ptr;
    static inline std::exception_ptr exception;

    explicit Private(MMAPR3000Bus::Callback* callback)
        : callback{callback}
    {}

    ~Private();

    // Use an initialization function instead of a constructor so that the destructor always runs.
    void init();

    uint32_t sigsegv_hanlder(void* ptr, AccessType type, AccessWidth width, uint32_t value = 0);

    void sigsegv_hanlder(ucontext_t* ucp);
    void sigbus_hanlder(ucontext_t* ucp);

    uint8_t* mem_space = nullptr;
    int ram_memfd = -1;
    int spad_memfd = -1;
    MMAPR3000Bus::Callback* callback;
};

void MMAPR3000Bus::Private::init()
{
    if (signal_ptr) {
        throw std::runtime_error{"The signal handler is already installed!"};
    }

    signal_ptr = this;
    install_handler(SIGSEGV, "SIGSEGV", static_sigsegv_hanlder);
    install_handler(SIGBUS, "SIGBUS", static_sigbus_hanlder);

    mem_space = (uint8_t*)mmap(nullptr, MEMORY_SPACE_SIZE, 0, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    if (!mem_space) {
        throw_errno("Can't get 4GB memory block");
    }

    map_memory("psx-ram", ram_memfd, mem_space, PSX_RAM_SIZE, PSX_RAM_ADDRS);
    map_memory("psx-scratchpad", spad_memfd, mem_space, PSX_SPAD_SIZE, PSX_SPAD_ADDRS);
}

MMAPR3000Bus::Private::~Private()
{
    unmap_memory(ram_memfd, mem_space, PSX_RAM_SIZE, PSX_RAM_ADDRS);
    unmap_memory(spad_memfd, mem_space, PSX_SPAD_SIZE, PSX_SPAD_ADDRS);
    if (mem_space) {
        munmap(mem_space, MEMORY_SPACE_SIZE);
    }
    uninstall_handler(SIGSEGV);
    uninstall_handler(SIGBUS);
    signal_ptr = nullptr;
}

void MMAPR3000Bus::Private::static_sigsegv_hanlder(int, siginfo_t* siginfo, void* ucp)
{
    signal_ptr->sigsegv_hanlder((ucontext_t*)ucp);
}

void MMAPR3000Bus::Private::static_sigbus_hanlder(int, siginfo_t* siginfo, void* ucp)
{
    signal_ptr->sigbus_hanlder((ucontext_t*)ucp);
}

uint32_t MMAPR3000Bus::Private::sigsegv_hanlder(void* ptr,
                                                AccessType type,
                                                AccessWidth width,
                                                uint32_t value)
{
    return 0;
}

void MMAPR3000Bus::Private::throw_from_handler()
{
    std::rethrow_exception(exception);
}

void MMAPR3000Bus::Private::sigsegv_hanlder(ucontext_t* ucontext)
{
    auto const reg_err = ucontext->uc_mcontext.gregs[REG_ERR];
    if ((reg_err
         & (X86_PF_PROT | X86_PF_USER | X86_PF_RSVD | X86_PF_INSTR | X86_PF_PK | X86_PF_SHSTK))
        != (X86_PF_PROT | X86_PF_USER)) {
        // Not the kind of fault we are expecting. Restore the default handler and return to let the
        // program crash.
        uninstall_handler(SIGSEGV);
        return;
    }

    bool const write_access = reg_err & X86_PF_WRITE;
    uintptr_t const rip = ucontext->uc_mcontext.gregs[REG_RIP];
    // The accessed addr is always in RDI if the fault happend at one of the supported instructions.
    void* const addr = (void*)ucontext->uc_mcontext.gregs[REG_RDI];

    try {
        if (write_access) {
            auto const value = ucontext->uc_mcontext.gregs[REG_RDX];
            if (rip == (uintptr_t)write_mem_impl<uint8_t>) {
                sigsegv_hanlder(addr, AccessType::WRITE, AccessWidth::A8, (uint8_t)value);
            } else if (rip == (uintptr_t)write_mem_impl<uint16_t>) {
                sigsegv_hanlder(addr, AccessType::WRITE, AccessWidth::A16, (uint16_t)value);
            } else if (rip == (uintptr_t)write_mem_impl<uint32_t>) {
                sigsegv_hanlder(addr, AccessType::WRITE, AccessWidth::A32, (uint32_t)value);
            } else {
                // Fault happened at an unknown instruction. Restore the default handler and return.
                uninstall_handler(SIGSEGV);
                return;
            }
        } else {
            auto& return_value = ucontext->uc_mcontext.gregs[REG_RAX];
            if (rip == (uintptr_t)read_mem_impl<uint8_t>) {
                return_value = sigsegv_hanlder(addr, AccessType::READ, AccessWidth::A8);
            } else if (rip == (uintptr_t)read_mem_impl<uint16_t>) {
                return_value = sigsegv_hanlder(addr, AccessType::READ, AccessWidth::A16);
            } else if (rip == (uintptr_t)read_mem_impl<uint32_t>) {
                return_value = sigsegv_hanlder(addr, AccessType::READ, AccessWidth::A32);
            } else {
                // Fault happened at an unknown instruction. Restore the default handler and return.
                uninstall_handler(SIGSEGV);
                return;
            }
        }

        // We have already emulated whatever the instruction was trying to write or read. If we just
        // return, the instruction will run again (and fault again). We need to point RIP to the
        // next instruction. But we know that the next instruction is always ret, so actually we can
        // pop the return address from the stack and return "manually" by incrementing RSP and
        // setting RIP to the return address.
        auto* const rsp_ptr = (uintptr_t const*)ucontext->uc_mcontext.gregs[REG_RSP];
        ucontext->uc_mcontext.gregs[REG_RIP] = *rsp_ptr;
        ucontext->uc_mcontext.gregs[REG_RSP] += sizeof(void*);
    } catch (...) {
        exception = std::current_exception();
        // Return to rethrow_exception instead of the original function. Since the functions we are
        // intersecting are C++ functions with noexcept(false), the return address is a landing pad,
        // so returning to rethrow_exception and immediately throwing will always work.
        ucontext->uc_mcontext.gregs[REG_RIP] = (greg_t)throw_from_handler;
    }
}

void MMAPR3000Bus::Private::sigbus_hanlder(ucontext_t* ucontext)
{}

MMAPR3000Bus::MMAPR3000Bus(Callback* callback)
    : _p{std::make_unique<Private>(callback)}
{
    _p->init();
    _mem_space = _p->mem_space;
}

MMAPR3000Bus::~MMAPR3000Bus() = default;
