#pragma once

#include "r3000.h"

class RegisterDispatcher {
public:
    void set_mem_space(uint8_t* mem_space) {
        _mem_space = mem_space;
    }

    void write_reg_32(R3000& emu, uint32_t addr, uint32_t value, uint32_t pre_image) const;
    void write_reg_16(R3000& emu, uint32_t addr, uint16_t value, uint16_t pre_image) const;
    void write_reg_8(R3000& emu, uint32_t addr, uint8_t value, uint8_t pre_image) const;

    uint32_t read_reg_32(R3000& emu, uint32_t addr, uint32_t pre_image) const;
    uint16_t read_reg_16(R3000& emu, uint32_t addr, uint16_t pre_image) const;
    uint8_t read_reg_8(R3000& emu, uint32_t addr, uint8_t pre_image) const;

private:
    uint8_t* _mem_space{};
};
