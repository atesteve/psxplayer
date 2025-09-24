#include "iop.h"
#include "util.h"

#include "libupse/upse.h"
#include "libupse/upse-r3000-abstract.h"
#include "libupse/upse-ps1-spu-base.h"

#include <fmt/format.h>
#include <zlib.h>

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string_view>
#include <concepts>
#include <filesystem>
#include <type_traits>

using namespace std::literals;

extern "C" {
upse_module_t* upse_load_psf2(FILE* f,
                              const char* path,
                              const upse_iofuncs_t* funcs,
                              emulation_control_t* control);

void upse_ps2_iop_call(upse_module_instance_t* ins);
}

namespace {
template<typename T>
struct malloc_ptr : public std::unique_ptr<T> {
    using std::unique_ptr<T>::unique_ptr;
};

template<typename T>
malloc_ptr(T*) -> malloc_ptr<T>;

using FILE_ptr = std::unique_ptr<FILE, decltype([](auto* ptr) { fclose(ptr); })>;

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
    auto* ptr = static_cast<PSF2*>(ins->opaque);
    delete ptr;
}

void finish_module_initialization(upse_xsf_t* xsf,
                                  auto const& mod,
                                  PSF2* psf2,
                                  uint32_t entry_point)
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

    auto* const ins = &mod->instance;

    // Setup PC, SP and return address.
    ins->cpustate.pc = to_le(entry_point);
    ins->cpustate.GPR.n.sp = to_le(0x801ffff0);
    ins->cpustate.GPR.n.ra = to_le(0x80000000);

    // Setup a spin loop at 0x80000000
    PSXMu32(ins, 0x80000000) = to_le(0x1000ffff); // b 0
    PSXMu32(ins, 0x80000004) = 0;                 // nop

    // Setup a call to a special iop function for module calls.
    PSXMu32(ins, 0x80000008) = to_le(0x03e00008); // jr $ra
    PSXMu32(ins, 0x8000000c) = to_le(0x2400ffff); // li $zero, -1

    psf2->imported_functions.try_emplace(
        0x80000008, LibFunction{"internal", 0x100, -1, &PSF2::iop_LoadStartModuleReturn});

    PSXMu32(ins, 0x80000010) = to_le(0x80000010);
    PSXMu32(ins, 0x80000014) = to_le(0x80000019);
    std::ranges::copy("psf2.irx", (char*)PSXM(ins, 0x80000018)); // argv[0]
    std::ranges::copy("vfs:/", (char*)PSXM(ins, 0x80000021));    // argv[1]

    ins->cpustate.GPR.n.a0 = to_le(2);          // argc
    ins->cpustate.GPR.n.a1 = to_le(0x80000008); // argv
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

void load_vfs_directory(PSF2::psf2_vfs& fs,
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

malloc_ptr<upse_xsf_t> load_psf2_file(FILE* f,
                                      std::filesystem::path const& path,
                                      PSF2::psf2_vfs& fs,
                                      int rec_level = 0)
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

    for (auto i = 0u; i < section_size; i += sizeof(Elf32_Rel)) {
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

} // namespace

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

        case 9: // SHT_REL - SHiT getting REaL
            handle_rel(ins, irx, base_addr, offset, size);
            break;

        case 0x70000080: // Sony .iopmod section.
            break;

        default:
            break;
        }
    }

    scan_imported_functions(ins, base_addr, max_addr);

    auto const entry_point = base_addr + from_le(header->e_entry);
    base_addr = max_addr;
    round_base_addr();

    return entry_point;
}

void PSF2::scan_imported_functions(upse_module_instance_t* ins,
                                   uint32_t start_addr,
                                   uint32_t end_addr)
{
    static constexpr auto EXPORT_FN_MAGIC = 0x41e00000;

    auto const ram_base = start_addr & 0x1fffff;
    auto const* const ram = reinterpret_cast<uint32_t*>(ins->psxM);

    for (auto i = ram_base / sizeof(uint32_t);
         i < (ram_base + (end_addr - start_addr)) / sizeof(uint32_t);
         ++i) {
        if (from_le(ram[i]) != EXPORT_FN_MAGIC) {
            continue;
        }

        if (auto const zero = from_le(ram[i + 1]); zero != 0) {
            continue;
        }

        auto const version = from_le(ram[i + 2]);
        auto const name = load_string(&ram[i + 3], 8);

        i += 5;

        while (true) {
            auto const jr = from_le(ram[i]);
            auto const addi = from_le(ram[i + 1]);

            if (jr != 0x03e00008) { // jr $ra
                break;
            }
            if ((addi & 0xffff0000) != 0x24000000) { // addi $zero $zero #imm
                break;
            }

            uint32_t const addr = i * sizeof(uint32_t) + 0x80000000;
            int const code = addi & 0xff;
            fmt::println("Found: {:#08x}: {} {:x} {}", addr, name, version, addi & 0xff);

            auto const it = builtin_iop_fns.find({name, code});

            imported_functions.try_emplace(addr,
                                           LibFunction{std::string{name},
                                                       version,
                                                       code,
                                                       it != builtin_iop_fns.cend()
                                                           ? std::optional{it->second}
                                                           : std::nullopt});

            i += 2;
        }
    }
}

upse_module_t*
    upse_load_psf2(FILE* f, const char* c_path, const upse_iofuncs_t*, emulation_control_t* control)
{
    std::filesystem::path path{c_path};

    // The returned object is expected to be created with malloc.
    malloc_ptr mod{(upse_module_t*)calloc(1, sizeof(upse_module_t))};

    auto* ins = &mod->instance;
    upse_ps1_init(ins);
    upse_ps1_reset(ins, UPSE_PSX_REV_PS2_IOP, control);

    PSF2::psf2_vfs fs{};
    auto xsf = load_psf2_file(f, path.parent_path(), fs);

    auto psf2 = std::make_unique<PSF2>();
    psf2->vfs = std::move(fs);

    // Load psf2.irx
    auto const entry_point = psf2->load_irx(ins, "/psf2.irx");

    finish_module_initialization(xsf.release(), mod, psf2.release(), entry_point);

    return mod.release();
}

void upse_ps2_iop_call(upse_module_instance_t* ins)
{
    auto* const ptr = static_cast<PSF2*>(ins->opaque);
    ptr->iop_call(ins);
}
