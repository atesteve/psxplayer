#include "iop.h"
#include "util.h"

#include "libupse/upse-ps1-memory-manager.h"

#include <string>

using namespace std::literals;

#define O_RDONLY 0x0001
#define O_WRONLY 0x0002
#define O_RDWR 0x0003

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

uint32_t PSF2::iop_open(upse_module_instance_t* ins)
{
    auto* const c_file_name = (char const*)PSXM(ins, from_le(ins->cpustate.GPR.n.a0));
    auto const flags = from_le(ins->cpustate.GPR.n.a1);

    if (flags != O_RDONLY) {
        // Implement just read only for the moment.
        return -1;
    }

    if (!c_file_name) {
        return -1;
    }

    auto const file_name = load_string(c_file_name, 256);
    auto const it = vfs.find("/"s + file_name);

    if (it == vfs.cend()) {
        // File not found.
        return -1;
    }

    auto const vfd = next_vfd;
    next_vfd++;

    vfd_store.try_emplace(vfd, Vfd{0, it->second.data(), (int)it->second.size()});

    return vfd;
}

uint32_t PSF2::iop_close(upse_module_instance_t* ins)
{
    int const vfd = from_le(ins->cpustate.GPR.n.a0);

    auto const it = vfd_store.find(vfd);
    if (it == vfd_store.cend()) {
        // Not valid vfd
        return -1;
    }
    vfd_store.erase(it);
    return 0;
}

uint32_t PSF2::iop_read(upse_module_instance_t* ins) { return -1; }

uint32_t PSF2::iop_lseek(upse_module_instance_t* ins)
{
    int const vfd = from_le(ins->cpustate.GPR.n.a0);
    int const offset = from_le(ins->cpustate.GPR.n.a1);
    int const whence = from_le(ins->cpustate.GPR.n.a2);

    auto const it = vfd_store.find(vfd);
    if (it == vfd_store.cend()) {
        // Not valid vfd
        return -1;
    }

    auto const new_offset = [&] {
        switch (whence) {
        case SEEK_SET:
            return offset;
        case SEEK_CUR:
            return it->second.p + offset;
        case SEEK_END:
            return it->second.size + offset;
        default:
            return -1;
        };
    }();

    if (new_offset < 0 || new_offset > it->second.size) {
        // Invalid offset
        return -1;
    }

    it->second.p = new_offset;
    return new_offset;
}
