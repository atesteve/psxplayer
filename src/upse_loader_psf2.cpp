#include "libupse/upse.h"
#include "libupse/upse-r3000-abstract.h"
#include "libupse/upse-ps1-spu-base.h"

#include <zlib.h>

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string_view>
#include <vector>
#include <bit>
#include <concepts>
#include <unordered_map>
#include <string>
#include <filesystem>
#include <type_traits>
#include <cstring>
#include <optional>

namespace stdx {

// https://stackoverflow.com/questions/76445860/implementation-of-stdstart-lifetime-as
template<class T>
    requires(std::is_trivially_copyable_v<T>)
T* start_lifetime_as(void* p) noexcept
{
    return std::launder(static_cast<T*>(std::memmove(p, p, sizeof(T))));
}

template<class T>
    requires(std::is_trivially_copyable_v<T>)
T const* start_lifetime_as(const void* p) noexcept
{
    return std::launder(static_cast<T const*>(std::memmove(const_cast<void*>(p), p, sizeof(T))));
}

} // namespace stdx

using namespace std::literals;

extern "C" {
upse_module_t* upse_load_psf2(FILE* f,
                              const char* path,
                              const upse_iofuncs_t* funcs,
                              emulation_control_t* control);
}

namespace {
template<typename T>
struct malloc_ptr : public std::unique_ptr<T> {
    using std::unique_ptr<T>::unique_ptr;
};

template<typename T>
malloc_ptr(T*) -> malloc_ptr<T>;

using FILE_ptr = std::unique_ptr<FILE, decltype([](auto* ptr) { fclose(ptr); })>;

using psf2_vfs = std::unordered_map<std::string, std::vector<uint8_t>>;

struct PSF2 {
    uint32_t load_irx(upse_module_instance_t* ins, std::string_view name);
    void iop_call(upse_module_instance_t* ins);

    uint32_t base_addr{0x80023f00}; // Magic number from HE.
    psf2_vfs vfs;
};

std::vector<uint8_t> read_file(FILE* f)
{
    std::vector<uint8_t> ret;

    auto const initial_pos = ftell(f);
    fseek(f, 0, SEEK_END);
    auto const end_pos = ftell(f);

    ret.resize(end_pos - initial_pos);
    fseek(f, initial_pos, SEEK_SET);
    fread(ret.data(), ret.size(), 1, f);

    return ret;
}

void free_psf2(upse_module_instance_t* ins)
{
    auto* ptr = (PSF2*)ins->opaque;
    delete ptr;
}

void finish_module_initialization(upse_xsf_t* xsf, auto const& mod, PSF2* psf2)
{
    // fill out our metadata struct.
    auto* psfi = (upse_psf_t*)calloc(1, sizeof(upse_psf_t));
    psfi->xsf = xsf;
    psfi->volume = upse_strtof(xsf->inf_volume) * 32;
    psfi->fade = upse_time_to_ms(xsf->inf_fade);
    psfi->stop = upse_time_to_ms(xsf->inf_length);
    psfi->title = xsf->inf_title;
    psfi->artist = xsf->inf_artist;
    psfi->copyright = xsf->inf_copy;
    psfi->game = xsf->inf_game;
    psfi->year = xsf->inf_year;

    upse_ps1_spu_setvolume((upse_spu_state_t*)mod->instance.spu, psfi->volume);
    upse_ps1_spu_setlength((upse_spu_state_t*)mod->instance.spu, psfi->stop, psfi->fade);
    psfi->length = psfi->stop + psfi->fade;
    psfi->rate = 44100;

    mod->instance.opaque = psf2;

    mod->metadata = psfi;
    mod->evloop_run = upse_r3000_cpu_execute;
    mod->evloop_stop = upse_ps1_spu_stop;
    mod->evloop_render = upse_r3000_cpu_execute_render;
    mod->evloop_setcb = upse_ps1_spu_set_audio_callback;
    mod->evloop_seek = upse_ps1_spu_seek;
    mod->evloop_tell_seek = upse_ps1_spu_tell_seek;
    mod->evloop_free_opaque = free_psf2;
}

template<std::integral Int>
Int from_le(Int i)
{
    if constexpr (std::endian::native == std::endian::big) {
        return std::byteswap(i);
    } else {
        return i;
    }
}

template<std::integral Int>
Int load(uint8_t const* buf)
{
    Int ret{};

    ret = (static_cast<Int>(buf[0]) & 0xff);
    if constexpr (sizeof(Int) > 1) {
        ret |= (static_cast<Int>(buf[1]) & 0xff) << 8;
    }
    if constexpr (sizeof(Int) > 2) {
        ret |= (static_cast<Int>(buf[2]) & 0xff) << 16;
        ret |= (static_cast<Int>(buf[3]) & 0xff) << 24;
    }

    return from_le(ret);
}

std::string_view load_string(auto const& buf, size_t max_size)
{
    std::string_view str{(char*)&buf[0], max_size};
    return str.substr(0, str.find_first_of('\0'));
}

template<size_t N>
std::string_view load_string(char const (&buf)[N])
{
    std::string_view str{buf, N};
    return str.substr(0, str.find_first_of('\0'));
}

std::vector<uint8_t> load_vfs_file(std::basic_string_view<uint8_t> buffer,
                                   size_t offset,
                                   size_t uncompressed_size,
                                   size_t block_size)
{
    std::vector<uint8_t> ret;
    ret.resize(uncompressed_size);

    auto const size_table_num_entries = (uncompressed_size + block_size - 1) / block_size;
    size_t block_offset = offset + size_table_num_entries * sizeof(uint32_t);
    size_t uncompressed_offset{};

    for (auto i = 0u; i < size_table_num_entries * sizeof(uint32_t); i += sizeof(uint32_t)) {
        auto const block_size = load<uint32_t>(&buffer[offset + i]);
        size_t block_uncompressed_size = ret.size() - uncompressed_offset;
        auto const err = uncompress(ret.data() + uncompressed_offset,
                                    &block_uncompressed_size,
                                    &buffer[block_offset],
                                    block_size);
        if (err != Z_OK) {
            break;
        }
        uncompressed_offset += block_uncompressed_size;
        block_offset += block_size;
    }

    return ret;
}

void load_vfs_directory(psf2_vfs& fs,
                        std::string path,
                        std::basic_string_view<uint8_t> buffer,
                        size_t offset = 0)
{
    static constexpr auto dir_entry_size = 48u;
    static constexpr auto file_name_size = 36u;

    auto const n_entries = load<uint32_t>(&buffer[offset]);
    for (auto i = 0u; i < n_entries * dir_entry_size; i += dir_entry_size) {
        size_t const dir_offset = offset + sizeof(uint32_t) + i;

        auto const name = load_string(&buffer[dir_offset], file_name_size);
        auto const data_offset = load<uint32_t>(&buffer[dir_offset + file_name_size]);
        auto const uncompressed_size = load<uint32_t>(&buffer[dir_offset + file_name_size + 4]);
        auto const block_size = load<uint32_t>(&buffer[dir_offset + file_name_size + 8]);

        if (data_offset == 0 && uncompressed_size == 0 && block_size == 0) {
            // Empty file.
            fs.try_emplace(path + name);
            continue;
        }

        if (data_offset != 0 && uncompressed_size == 0 && block_size == 0) {
            // Subdirectory.
            load_vfs_directory(fs, path + name + "/", buffer, data_offset);
            continue;
        }

        // Normal file.
        auto file_content = load_vfs_file(buffer, data_offset, uncompressed_size, block_size);
        fs.try_emplace(path + name, std::move(file_content));
    }
}

malloc_ptr<upse_xsf_t>
    load_psf2_file(FILE* f, std::filesystem::path const& path, psf2_vfs& fs, int rec_level = 0)
{
    static constexpr int MAX_REC_LEVEL = 9;

    auto const buffer = read_file(f);
    uint8_t* out_p{};
    uint64_t outlen;
    malloc_ptr xsf{upse_xsf_decode(buffer.data(), buffer.size(), &out_p, &outlen)};
    malloc_ptr out{out_p};

    load_vfs_directory(fs, "/", {xsf->res_section, xsf->res_size});

    auto const load_lib = [&](auto&& lib) {
        auto const psf2_lib_name = load_string(lib);
        if (rec_level >= MAX_REC_LEVEL || psf2_lib_name.empty()) {
            return false;
        }

        FILE_ptr lib_f{fopen((path / psf2_lib_name).c_str(), "rb")};
        if (lib_f) {
            load_psf2_file(lib_f.get(), path, fs, rec_level + 1);
        }

        return true;
    };

    load_lib(xsf->lib);
    for (auto&& lib_aux : xsf->libaux) {
        if (!load_lib(lib_aux)) {
            break;
        }
    }

    return xsf;
}

constexpr size_t EI_NIDENT = 16;
using Elf32_Half = uint16_t;
using Elf32_Word = uint32_t;
using Elf32_Addr = uint32_t;
using Elf32_Off = uint32_t;

struct Elf32_Ehdr {
    unsigned char e_ident[EI_NIDENT];
    Elf32_Half e_type;
    Elf32_Half e_machine;
    Elf32_Word e_version;
    Elf32_Addr e_entry;
    Elf32_Off e_phoff;
    Elf32_Off e_shoff;
    Elf32_Word e_flags;
    Elf32_Half e_ehsize;
    Elf32_Half e_phentsize;
    Elf32_Half e_phnum;
    Elf32_Half e_shentsize;
    Elf32_Half e_shnum;
    Elf32_Half e_shstrndx;
};

struct Elf32_Shdr {
    Elf32_Word sh_name;
    Elf32_Word sh_type;
    Elf32_Word sh_flags;
    Elf32_Addr sh_addr;
    Elf32_Off sh_offset;
    Elf32_Word sh_size;
    Elf32_Word sh_link;
    Elf32_Word sh_info;
    Elf32_Word sh_addralign;
    Elf32_Word sh_entsize;
};

struct Elf32_Rel {
    Elf32_Addr r_offset;
    Elf32_Word r_info;
};

void handle_rel(upse_module_instance_t* ins,
                std::vector<uint8_t> const& irx,
                uint32_t base_addr,
                uint32_t section_offset,
                uint32_t section_size)
{
    std::optional<uint32_t> rel_target_hi{};
    std::optional<uint32_t> rel_target_lo{};

    for (auto i = 0u; i < section_size * sizeof(Elf32_Rel); i += sizeof(Elf32_Rel)) {
        auto const* const rel = stdx::start_lifetime_as<Elf32_Rel>(irx.data() + section_offset + i);
        auto const offset = from_le(rel->r_offset);
        auto const info = from_le(rel->r_info);
        auto const target = base_addr + offset;
        auto const type = info & 0xff; // LSB of the info field contains the relocation type.
        uint32_t temp;

        switch (type) {
        case 2: // Pure 32-bit relocation.
            PSXMu32(ins, target) += base_addr;
            break;

        case 4: // JMP instruction field (28 bits with two zero implied LSB)
            temp = PSXMu32(ins, target) & 0x03ffffff;
            temp <<= 2;
            temp += base_addr;
            temp >>= 2;
            PSXMu32(ins, target) &= 0xfc000000;
            PSXMu32(ins, target) |= temp & 0x03ffffff;
            break;

        case 5: // Two-step 32-bit immediate, hi bits.
            [[fallthrough]];
        case 6: // Two-step 32-bit immediate, lo bits.
            if (type == 5) {
                rel_target_hi = target;
            } else {
                rel_target_lo = target;
            }

            if (!rel_target_hi || !rel_target_lo) {
                break;
            }

            temp = PSXMu32(ins, *rel_target_lo) & 0xffff;
            temp |= (PSXMu32(ins, *rel_target_hi) & 0xffff) << 16;
            temp += base_addr;

            PSXMu32(ins, *rel_target_lo) =
                (PSXMu32(ins, *rel_target_lo) & 0xffff0000) | (temp & 0xffff);
            PSXMu32(ins, *rel_target_hi) =
                (PSXMu32(ins, *rel_target_hi) & 0xffff0000) | ((temp >> 16) & 0xffff);

            rel_target_hi = std::nullopt;
            rel_target_lo = std::nullopt;
            break;
        }
    }
}

uint32_t PSF2::load_irx(upse_module_instance_t* ins, std::string_view name)
{
    auto const it = vfs.find(std::string{name});
    if (it == vfs.cend()) {
        return 0xffffffff;
    }

    auto const& irx = it->second;

    // Check magic value.
    if (load_string(irx, 4) != "\177ELF"sv) {
        // Not an ELF
        return 0xffffffff;
    }

    uint32_t max_addr = 0;

    auto const* const header = stdx::start_lifetime_as<Elf32_Ehdr>(irx.data());
    auto const sections_offset = from_le(header->e_shoff);
    auto const section_size = from_le(header->e_shentsize);
    auto const n_sections = from_le(header->e_shentsize);

    for (auto i = 0u; i < n_sections; ++i) {
        auto const section_offset = sections_offset + i * section_size;
        auto const* const section =
            stdx::start_lifetime_as<Elf32_Shdr>(irx.data() + section_offset);

        auto const addr = from_le(section->sh_addr);
        auto const size = from_le(section->sh_size);
        auto const offset = from_le(section->sh_offset);

        switch (from_le(section->sh_type)) {
        case 0: // SHT_NULL
            break;

        case 1: // SHT_PROGBITS
            upse_ps1_memory_load(ins, base_addr + addr, size, irx.data() + offset);
            max_addr = std::max(max_addr, base_addr + addr + size);
            break;

        case 2: // SHT_SYMTAB
        case 3: // SHT_STRTAB
            break;

        case 8: // SHT_NOBITS
            upse_ps1_memory_clear(ins, base_addr + addr, size);
            max_addr = std::max(max_addr, base_addr + addr + size);
            break;

        case 9: // SHT_REL
            handle_rel(ins, irx, base_addr, offset, size);
            break;

        case 0x70000080: // Sony .iopmod section.
            break;

        default:
            break;
        }
    }

    auto const entry_point = base_addr + from_le(header->e_entry);
    base_addr = ((max_addr + 3) / 4) * 4; // Round up to multiple of 4.

    return entry_point;
}

} // namespace

upse_module_t*
    upse_load_psf2(FILE* f, const char* c_path, const upse_iofuncs_t*, emulation_control_t* control)
{
    std::filesystem::path path{c_path};

    // The returned object is expected to be created with malloc.
    malloc_ptr mod{(upse_module_t*)calloc(1, sizeof(upse_module_t))};

    auto* ins = &mod->instance;
    upse_ps1_init(ins);
    upse_ps1_reset(ins, UPSE_PSX_REV_PS2_IOP, control);

    psf2_vfs fs{};
    auto xsf = load_psf2_file(f, path.parent_path(), fs);

    auto psf2 = std::make_unique<PSF2>();
    psf2->vfs = std::move(fs);

    // Load psf2.irx
    auto const entry_point = psf2->load_irx(ins, "/psf2.irx");

    finish_module_initialization(xsf.release(), mod, psf2.release());
    ins->cpustate.pc = entry_point;
    ins->cpustate.GPR.n.sp = 0x801ffff0;

    return mod.release();
}
