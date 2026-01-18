// SPDX-FileCopyrightText: 2026 Aitor Esteve Alvarado
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "r3000.h"

struct R3000CoreConfig {
    FaultCheck fault_check{};
    AlignmentCheck alignment_check{};
};

template<R3000CoreConfig>
class R3000Core : public R3000 {
public:
    explicit R3000Core();
    ~R3000Core() override;

    uint8_t read_mem_u8(r3000_ptr_t addr) const override;
    uint16_t read_mem_u16(r3000_ptr_t addr) const override;
    uint32_t read_mem_u32(r3000_ptr_t addr) const override;

    void write_mem_u8(r3000_ptr_t addr, uint8_t value) override;
    void write_mem_u16(r3000_ptr_t addr, uint16_t value) override;
    void write_mem_u32(r3000_ptr_t addr, uint32_t value) override;

    void set_regs(uint32_t sp, uint32_t pc) override;
    void run() override;

    Core& core() override;
    Core const& core() const override;

    uint8_t* get_buffer_checked(r3000_ptr_t addr, uint32_t size, bool ram) const override;

    uint32_t& istat() override;
    uint32_t& imask() override;

    void return_from_exception() override;

    Bios* get_bios() override;
    DMA* get_dma() override;
    SPU* get_spu() override;
    TimerHandler* get_timers() override;
    Timing* get_timing() override;

private:
    struct Private;
    std::unique_ptr<Private> p;
};

class R3000Exception : public std::logic_error {
public:
    using std::logic_error::logic_error;
};
