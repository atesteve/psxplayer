// SPDX-FileCopyrightText: 2026 Aitor Esteve Alvarado
// SPDX-License-Identifier: GPL-3.0-only

#include "soft-r3000bus.h"
#include "register-dispatcher.h"
#include "util/util.h"

#include <fmt/format.h>

#include <array>

namespace {
constexpr uint32_t PSX_IO_SIZE = 8192;
constexpr auto PSX_IO_ADDRS = std::to_array<r3000_ptr_t>({0x1f801000, 0x9f801000, 0xbf801000});
} // namespace

struct SoftR3000Bus::Private {
    // Use an initialization function instead of a constructor so that the destructor always runs.
    void init(R3000* emu);

    R3000* emu;
    RegisterDispatcher dispatcher;
    alignas(uint32_t) std::array<uint8_t, 2 * 1024 * 1024 + 4> ram;
    alignas(uint32_t) std::array<uint8_t, 1024 + 4> scratchpad;
    alignas(uint32_t) std::array<uint8_t, 8 * 1024> device_mem{};
};

void SoftR3000Bus::Private::init(R3000* emu)
{
    this->emu = emu;
    dispatcher.init(emu);
    dispatcher.set_mem_space(device_mem.data());
}

uint8_t* SoftR3000Bus::get_mem_ptr()
{
    return _ram;
}

uint8_t* SoftR3000Bus::get_device_mem_ptr()
{
    return _p->device_mem.data();
}

void SoftR3000Bus::init(R3000* emu)
{
    _p->init(emu);
    _ram = _p->ram.data();
    _scratchpad = _p->scratchpad.data();
}

template<std::integral Int>
Int SoftR3000Bus::handle_fault(r3000_ptr_t addr, AccessType type, Int value)
{
    if (std::ranges::any_of(PSX_IO_ADDRS, [&](r3000_ptr_t hw_base_addr) {
            return addr >= hw_base_addr && addr + sizeof(Int) <= hw_base_addr + PSX_IO_SIZE;
        })) {
        // This is an address pointing to the lower mirror.
        auto const reg_addr = 0x1f800000u | (addr & (PSX_IO_SIZE - 1));

        if (type == AccessType::WRITE) {
            _p->dispatcher.write_reg<Int>(reg_addr, value);
            return 0;
        } else {
            return _p->dispatcher.read_reg<Int>(reg_addr);
        }
    }

    throw AddressException{addr, type, int_width<Int>};
}

void SoftR3000Bus::take_snapshot(SnapshotSerializer& s)
{
    s.serialize(_p->ram);
    s.serialize(_p->scratchpad);
    s.serialize(_p->device_mem);
}

void SoftR3000Bus::restore_snapshot(SnapshotDeserializer& d)
{
    d.deserialize(_p->ram);
    d.deserialize(_p->scratchpad);
    d.deserialize(_p->device_mem);
}

SoftR3000Bus::SoftR3000Bus()
    : _p{std::make_unique<Private>()}
{}
SoftR3000Bus::~SoftR3000Bus() = default;

template uint8_t
    SoftR3000Bus::handle_fault(r3000_ptr_t addr, AccessType access_type, uint8_t value);
template uint16_t
    SoftR3000Bus::handle_fault(r3000_ptr_t addr, AccessType access_type, uint16_t value);
template uint32_t
    SoftR3000Bus::handle_fault(r3000_ptr_t addr, AccessType access_type, uint32_t value);
