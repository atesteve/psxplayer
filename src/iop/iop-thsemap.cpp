// SPDX-FileCopyrightText: 2026 Aitor Esteve Alvarado
// SPDX-License-Identifier: GPL-3.0-only

#include "iop.h"
#include "util.h"

#include "libupse/upse-ps1-memory-manager.h"

// struct iop_sema_t {
//     uint32_t attr;
//     uint32_t option;
//     int initial;
//     int max;
// };

// std::optional<uint32_t> PSF2::iop_CreateSema(upse_module_instance_t* ins)
// {
//     auto* const ptr = (iop_sema_t*)PSXM(ins, from_le(ins->cpustate.GPR.n.a0));
//     return 0x55555555;
// }
