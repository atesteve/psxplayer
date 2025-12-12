#include "mmap-r3000bus.h"

#include <fmt/format.h>

#include <sys/mman.h>
#include <unistd.h>
#include <string.h>

#include <stdexcept>
#include <string_view>
#include <array>

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

} // namespace

struct MMAPR3000Bus::Private {
    ~Private();

    // Use an initialization function instead of a constructor so that the destructor always runs.
    void init();

    uint8_t* mem_space = nullptr;
    int ram_memfd = -1;
    int spad_memfd = -1;
};

void MMAPR3000Bus::Private::init()
{
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
}

MMAPR3000Bus::MMAPR3000Bus()
    : _p{std::make_unique<Private>()}
{
    _p->init();
    _mem_space = _p->mem_space;
}

MMAPR3000Bus::~MMAPR3000Bus() = default;
