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

uint32_t PSF2::iop_read(upse_module_instance_t* ins)
{
    int const vfd = from_le(ins->cpustate.GPR.n.a0);
    auto* const buf = (char*)PSXM(ins, from_le(ins->cpustate.GPR.n.a1));
    uint32_t const count = from_le(ins->cpustate.GPR.n.a2);

    auto const it = vfd_store.find(vfd);
    if (it == vfd_store.cend()) {
        // Not valid vfd
        return -1;
    }
    auto& vfd_d = it->second;

    if (!buf) {
        return -1;
    }

    auto const start_p = vfd_d.p;
    auto const end_p = std::min<int>(vfd_d.p + count, vfd_d.size);
    auto const ret = end_p - start_p;
    vfd_d.p = end_p;

    std::copy(vfd_d.base + start_p, vfd_d.base + end_p, buf);

    return ret;
}

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
    auto& vfd_d = it->second;

    auto const new_offset = [&] {
        switch (whence) {
        case SEEK_SET:
            return offset;
        case SEEK_CUR:
            return vfd_d.p + offset;
        case SEEK_END:
            return vfd_d.size + offset;
        default:
            return -1;
        };
    }();

    if (new_offset < 0 || new_offset > vfd_d.size) {
        // Invalid offset
        return -1;
    }

    vfd_d.p = new_offset;
    return new_offset;
}

struct iop_device_ops_t {
	uint32_t init;
	uint32_t deinit;
	uint32_t format;
	uint32_t open;
	uint32_t close;
	uint32_t read;
	uint32_t write;
	uint32_t lseek;
	uint32_t ioctl;
	uint32_t remove;
	uint32_t mkdir;
	uint32_t rmdir;
	uint32_t dopen;
	uint32_t dclose;
	uint32_t dread;
	uint32_t getstat;
	uint32_t chstat;
};

struct iop_device_t {
	uint32_t name;
	uint32_t type;
	uint32_t version;
	uint32_t desc;
	uint32_t ops;
};

uint32_t PSF2::iop_AddDrv(upse_module_instance_t* ins)
{
    auto* const table = (iop_device_t*)PSXM(ins, from_le(ins->cpustate.GPR.n.a0));
    auto* const name = (char*)PSXM(ins, from_le(table->name));
    auto* const desc = (char*)PSXM(ins, from_le(table->desc));
    auto* const ops = (iop_device_ops_t*)PSXM(ins, from_le(table->ops));
    return 0;
}

uint32_t PSF2::iop_DelDrv(upse_module_instance_t* ins)
{
    auto* const name = (char*)PSXM(ins, from_le(ins->cpustate.GPR.n.a0));
    return 0;
}
