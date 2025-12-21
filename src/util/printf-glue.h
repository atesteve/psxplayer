#pragma once

#include "xprintf/xprintf.h"

#include <cstdint>

struct xva_list {
    virtual ~xva_list() = default;
    virtual int next_param() = 0;
    virtual uint32_t* gpr() = 0;
    virtual uint32_t* get_u32_ptr(uint32_t addr) = 0;
    virtual char* get_char_ptr(uint32_t addr) = 0;
    virtual uint32_t read(uint32_t addr) = 0;
};
