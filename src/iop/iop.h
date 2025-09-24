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

    using iop_handler = void (PSF2::*)(upse_module_instance_t* ins);
    using iop_table_key = std::pair<std::string_view, int>;

    static std::unordered_map<iop_table_key, iop_handler> builtin_iop_fns;

    uint32_t load_irx(upse_module_instance_t* ins, std::string_view name);
    void scan_imported_functions(upse_module_instance_t* ins,
                                 uint32_t start_addr,
                                 uint32_t end_addr);

    void iop_call(upse_module_instance_t* ins);
    void iop_printf(upse_module_instance_t* ins);

    uint32_t base_addr{0x80023f00}; // Magic number from HE.
    psf2_vfs vfs;
    imported_functions_t imported_functions;
};

template<>
struct std::hash<PSF2::iop_table_key>
{
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
