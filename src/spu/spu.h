#pragma once

#include "core/r3000.h"

#include <cstdint>
#include <memory>

class SPU {
public:
    explicit SPU();
    ~SPU();

    void init(R3000* emu);

    void write_reg(r3000_ptr_t addr, uint16_t value);
    uint16_t read_reg(r3000_ptr_t addr);

    uint64_t dma_write(r3000_ptr_t addr, uint32_t nbytes);
    uint64_t dma_read(r3000_ptr_t addr, uint32_t nbytes);
private:
    struct Private;
    std::unique_ptr<Private> _p;
};
