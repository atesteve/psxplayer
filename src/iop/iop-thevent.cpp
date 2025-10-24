#include "iop.h"
#include "util.h"

#include "libupse/upse-ps1-memory-manager.h"

// struct iop_event_t {
//     uint32_t attr;
//     uint32_t option;
//     uint32_t bits;
// };

// std::optional<uint32_t> PSF2::iop_CreateEventFlag(upse_module_instance_t* ins) {
//     auto* const ptr = (iop_event_t*)PSXM(ins, from_le(ins->cpustate.GPR.n.a0));
//     return 0x55555555;
// }
