#pragma once

#include "libupse/upse.h"

#include <string>
#include <cstdint>
#include <unordered_map>
#include <vector>
#include <utility>
#include <string_view>
#include <variant>
#include <optional>

struct LibCallPoint;

struct irx_export_table;

using iop_table_key = std::pair<std::string_view, int>;

template<>
struct std::hash<iop_table_key> {
    std::size_t operator()(const iop_table_key& s) const noexcept
    {
        std::size_t h1 = std::hash<std::string_view>{}(s.first);
        std::size_t h2 = std::hash<int>{}(s.second);
        return h1 ^ (h2 << 1); // or use boost::hash_combine
    }
};

struct PSF2 {
    using psf2_vfs = std::unordered_map<std::string, std::vector<uint8_t>>;
    using imported_functions_t = std::unordered_map<uint32_t, LibCallPoint>;

    using iop_builtin_handler = std::optional<uint32_t> (PSF2::*)(upse_module_instance_t* ins);

    static std::unordered_map<iop_table_key, iop_builtin_handler> builtin_iop_fns;

    uint32_t load_irx(upse_module_instance_t* ins, std::string_view name);
    void scan_imported_functions(upse_module_instance_t* ins,
                                 uint32_t start_addr,
                                 uint32_t end_addr);

    void iop_call(upse_module_instance_t* ins);

    template<typename T>
    struct PointerArg {
        T ptr;
        uint32_t raw_ptr;
    };

    template<auto F>
    std::optional<uint32_t> iop_builtin(upse_module_instance_t* ins);

    std::optional<uint32_t> iop_printf(upse_module_instance_t* ins);

    int iop_open(PointerArg<char const*> name, int flags);
    int iop_close(int fd);
    int iop_read(int fd, PointerArg<void*> ptr, uint32_t count);
    int iop_lseek(int fd, int offset, int whence);
    std::optional<uint32_t> iop_AddDrv(upse_module_instance_t* ins);
    std::optional<uint32_t> iop_DelDrv(upse_module_instance_t* ins);

    uint32_t iop_AllocSysMemory(int mode, int size, PointerArg<void*> ptr);
    int iop_FreeSysMemory(PointerArg<void*> ptr);

    int iop_LoadStartModule(upse_module_instance_t* ins,
                            PointerArg<char const*> name,
                            int arglen,
                            PointerArg<char const*> args,
                            PointerArg<int*> result);
    std::optional<uint32_t> iop_LoadStartModuleReturn(upse_module_instance_t* ins);

    std::optional<uint32_t> iop_RegisterIntrHandler(upse_module_instance_t* ins);
    std::optional<uint32_t> iop_ReleaseIntrHandler(upse_module_instance_t* ins);
    std::optional<uint32_t> iop_EnableIntr(upse_module_instance_t* ins);
    std::optional<uint32_t> iop_DisableIntr(upse_module_instance_t* ins);
    std::optional<uint32_t> iop_CpuSuspendIntr(upse_module_instance_t* ins);
    std::optional<uint32_t> iop_CpuResumeIntr(upse_module_instance_t* ins);

    int iop_RegisterLibraryEntries(upse_module_instance_t* ins,
                                   PointerArg<irx_export_table const*> exports);

    uint32_t iop_memset(PointerArg<void*> ptr, int c, uint32_t n);
    void iop_bzero(PointerArg<void*> ptr, uint32_t n);
    uint32_t iop_strcpy(PointerArg<char*> dst, PointerArg<char const*> src);
    uint32_t iop_strlen(PointerArg<char const*> str);
    uint32_t iop_strncpy(PointerArg<char*> dst, PointerArg<char const*> src, uint32_t size);
    int32_t iop_strtol(upse_module_instance_t* ins,
                       PointerArg<char const*> nptr,
                       PointerArg<char**> endptr,
                       int base);

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
    std::unordered_map<iop_table_key, uint32_t> exported_iop_fns;
    std::unordered_map<int, Vfd> vfd_store;
    int next_vfd{3};

    struct {
        uint32_t ra;
        uint32_t a0;
        uint32_t a1;
        uint32_t a2;
        uint32_t a3;
        uint32_t sp;
    } loadStartModule_saved_state;
};

struct LibCallPoint {
    std::string name;
    uint32_t version;
    int index;
    std::variant<std::nullptr_t, PSF2::iop_builtin_handler, uint32_t> handler{};
};
