#pragma once

#include "core/r3000.h"

#include <cstdint>
#include <memory>

class SPU {
public:
    explicit SPU();
    ~SPU();

    void init(R3000* emu);

    void write_register(r3000_ptr_t addr, uint16_t value);
    uint16_t read_register(r3000_ptr_t addr);
private:
    struct Private;
    std::unique_ptr<Private> _p;
};
