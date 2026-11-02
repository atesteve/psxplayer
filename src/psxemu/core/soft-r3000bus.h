// SPDX-FileCopyrightText: 2026 Aitor Esteve Alvarado
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "r3000.h"

#include <memory>
#include <concepts>
#include <cstdint>

class SoftR3000Bus {
    enum class AddressType {
        RAM,
        SCRPAD,
        OTHER,
    };

public:
    explicit SoftR3000Bus();
    ~SoftR3000Bus();

    void init(R3000* emu);

    template<std::integral Int>
    Int read_mem(r3000_ptr_t addr)
    {
        switch (check_address(addr)) {
        case AddressType::RAM:
            return *get_ptr<Int>(addr, _ram, 0x1fffff);
        case AddressType::SCRPAD:
            return *get_ptr<Int>(addr, _scratchpad, 0x3ff);
        case AddressType::OTHER:
            return handle_fault<Int>(addr, AccessType::READ, 0);
        }
        std::unreachable();
    }

    template<std::integral Int>
    void write_mem(r3000_ptr_t addr, Int value)
    {
        switch (check_address(addr)) {
        case AddressType::RAM:
            *get_ptr<Int>(addr, _ram, 0x1fffff) = value;
            return;
        case AddressType::SCRPAD:
            *get_ptr<Int>(addr, _scratchpad, 0x3ff) = value;
            return;
        case AddressType::OTHER:
            handle_fault<Int>(addr, AccessType::WRITE, value);
            return;
        }
        std::unreachable();
    }

    template<std::integral Int>
    Int handle_fault(r3000_ptr_t addr, AccessType access_type, Int value);

    uint8_t* get_mem_ptr();
    uint8_t* get_device_mem_ptr();

    void take_snapshot(SnapshotSerializer& s);
    void restore_snapshot(SnapshotDeserializer& s);

private:
    static AddressType check_address(r3000_ptr_t addr)
    {
        static constexpr r3000_ptr_t ram_ranges[] = {0x00000000, 0x80000000, 0xa0000000};
        static constexpr r3000_ptr_t scrpad_ranges[] = {0x1f800000, 0x9f800000};
        if (std::ranges::contains(ram_ranges, addr & 0xff800000)) {
            return AddressType::RAM;
        }
        if (std::ranges::contains(scrpad_ranges, addr & 0xfffffc00)) {
            return AddressType::SCRPAD;
        }
        return AddressType::OTHER;
    }

    template<std::integral Int>
    static Int* get_ptr(r3000_ptr_t addr, uint8_t* ptr, r3000_ptr_t mask)
    {
        auto* ptr_u8 = ptr + (addr & mask);
        return reinterpret_cast<Int*>(ptr_u8);
    }

    struct Private;
    std::unique_ptr<Private> _p;
    uint8_t* _ram;
    uint8_t* _scratchpad;
};
