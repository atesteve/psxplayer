#include "mmap-r3000bus.h"

template<std::integral Int>
Int MMAPR3000Bus::read_mem(uint32_t addr)
{
    return *(volatile Int*)&_mem_space[addr];
}

template<std::integral Int>
void MMAPR3000Bus::write_mem(uint32_t addr, Int value)
{
    *((volatile Int*)&_mem_space[addr]) = value;
}

template uint8_t MMAPR3000Bus::read_mem<uint8_t>(uint32_t addr);
template int8_t MMAPR3000Bus::read_mem<int8_t>(uint32_t addr);
template uint16_t MMAPR3000Bus::read_mem<uint16_t>(uint32_t addr);
template int16_t MMAPR3000Bus::read_mem<int16_t>(uint32_t addr);
template uint32_t MMAPR3000Bus::read_mem<uint32_t>(uint32_t addr);

template void MMAPR3000Bus::write_mem<uint8_t>(uint32_t addr, uint8_t value);
template void MMAPR3000Bus::write_mem<uint16_t>(uint32_t addr, uint16_t value);
template void MMAPR3000Bus::write_mem<uint32_t>(uint32_t addr, uint32_t value);
