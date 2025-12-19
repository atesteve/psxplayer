#include "register-dispatcher.h"

void RegisterDispatcher::write_reg_32(R3000& emu,
                                      uint32_t addr,
                                      uint32_t value,
                                      uint32_t pre_image) const
{
    *(uint32_t*)(_mem_space + addr) = value;
}

void RegisterDispatcher::write_reg_16(R3000& emu,
                                      uint32_t addr,
                                      uint16_t value,
                                      uint16_t pre_image) const
{
    *(uint16_t*)(_mem_space + addr) = value;
}

void RegisterDispatcher::write_reg_8(R3000& emu,
                                     uint32_t addr,
                                     uint8_t value,
                                     uint8_t pre_image) const
{
    _mem_space[addr] = value;
}

uint32_t RegisterDispatcher::read_reg_32(R3000& emu, uint32_t addr, uint32_t pre_image) const
{
    return pre_image;
}

uint16_t RegisterDispatcher::read_reg_16(R3000& emu, uint32_t addr, uint16_t pre_image) const
{
    return pre_image;
}

uint8_t RegisterDispatcher::read_reg_8(R3000& emu, uint32_t addr, uint8_t pre_image) const
{
    return pre_image;
}
