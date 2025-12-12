#pragma once

#include <memory>
#include <concepts>
#include <cstdint>

class MMAPR3000Bus {
public:
    explicit MMAPR3000Bus();
    ~MMAPR3000Bus();

    template<std::integral Int>
    Int read_mem(uint32_t addr)
    {
        return *(volatile Int*)&_mem_space[addr];
    }

    template<std::integral Int>
    void write_mem(uint32_t addr, Int value)
    {
        *((volatile Int*)&_mem_space[addr]) = value;
    }

private:
    struct Private;
    volatile uint8_t* _mem_space;
    std::unique_ptr<Private> _p;
};
