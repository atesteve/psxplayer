// SPDX-FileCopyrightText: 2026 Aitor Esteve Alvarado
// SPDX-License-Identifier: GPL-3.0-only

#include "iop.h"
#include "util.h"

std::optional<uint32_t> PSF2::iop_RegisterIntrHandler(upse_module_instance_t*) { return 0; }
std::optional<uint32_t> PSF2::iop_ReleaseIntrHandler(upse_module_instance_t*) { return 0; }
std::optional<uint32_t> PSF2::iop_EnableIntr(upse_module_instance_t*) { return 0; }
std::optional<uint32_t> PSF2::iop_DisableIntr(upse_module_instance_t*) { return 0; }
std::optional<uint32_t> PSF2::iop_CpuSuspendIntr(upse_module_instance_t*) { return 0; }
std::optional<uint32_t> PSF2::iop_CpuResumeIntr(upse_module_instance_t*) { return 0; }
