#include "iop.h"
#include "util.h"

#include <fmt/format.h>

#include "libupse/upse-ps1-memory-manager.h"

struct irx_export_table {
    uint32_t magic;
    uint32_t next;
    uint16_t version;
    uint16_t mode;
    char name[8];
    uint32_t fptrs;
};

uint32_t PSF2::iop_RegisterLibraryEntries(upse_module_instance_t* ins)
{
    static constexpr uint32_t IRX_EXPORT_MAGIC = 0x41c00000;

    auto* table = (irx_export_table const*)PSXM(ins, from_le(ins->cpustate.GPR.n.a0));

    while (table) {
        if (from_le(table->magic) != IRX_EXPORT_MAGIC) {
            return -1;
        }

        int const version = from_le(table->version);
        auto const name = load_string(table->name);

        int n = 0;
        auto const* fptr = &table->fptrs;
        while (*fptr) {
            exported_iop_fns.try_emplace({name, n}, from_le(*fptr));
            fptr++;
            n++;
        }

        fmt::println("Imported {} {:x}: {} functions", name, version, n);

        if (table->next) {
            table = (irx_export_table const*)PSXM(ins, from_le(table->next));
        } else {
            table = nullptr;
        }
    }

    return 0;
}
