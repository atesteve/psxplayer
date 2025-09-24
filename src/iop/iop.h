#pragma once

#include "libupse/upse.h"

#include <string>
#include <cstdint>
#include <unordered_map>
#include <vector>
#include <utility>
#include <string_view>
#include <optional>

struct LibFunction;

struct PSF2 {
    using psf2_vfs = std::unordered_map<std::string, std::vector<uint8_t>>;
    using imported_functions_t = std::unordered_map<uint32_t, LibFunction>;

    using iop_handler = uint32_t (PSF2::*)(upse_module_instance_t* ins);
    using iop_table_key = std::pair<std::string_view, int>;

    static std::unordered_map<iop_table_key, iop_handler> builtin_iop_fns;

    uint32_t load_irx(upse_module_instance_t* ins, std::string_view name);
    void scan_imported_functions(upse_module_instance_t* ins,
                                 uint32_t start_addr,
                                 uint32_t end_addr);

    void iop_call(upse_module_instance_t* ins);
    uint32_t iop_printf(upse_module_instance_t* ins);
    uint32_t iop_open(upse_module_instance_t* ins);
    uint32_t iop_close(upse_module_instance_t* ins);
    uint32_t iop_read(upse_module_instance_t* ins);
    uint32_t iop_lseek(upse_module_instance_t* ins);
    uint32_t iop_AllocSysMemory(upse_module_instance_t* ins);
    uint32_t iop_FreeSysMemory(upse_module_instance_t* ins);

    void round_base_addr()
    {
        base_addr = ((base_addr + 3) / 4) * 4; // Round up to multiple of 4.
    }

    struct Vfd {
        int p;
        const uint8_t* base;
        int size;
    };

    uint32_t base_addr{0x80023f00}; // Magic number from HE.
    psf2_vfs vfs;
    imported_functions_t imported_functions;
    std::unordered_map<int, Vfd> vfd_store;
    int next_vfd{3};
};

template<>
struct std::hash<PSF2::iop_table_key> {
    std::size_t operator()(const PSF2::iop_table_key& s) const noexcept
    {
        std::size_t h1 = std::hash<std::string_view>{}(s.first);
        std::size_t h2 = std::hash<int>{}(s.second);
        return h1 ^ (h2 << 1); // or use boost::hash_combine
    }
};

struct LibFunction {
    std::string name;
    uint32_t version;
    int index;
    std::optional<PSF2::iop_handler> handler;
};
