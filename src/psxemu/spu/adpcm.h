// SPDX-FileCopyrightText: 2026 Aitor Esteve Alvarado
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "psxemu/core/r3000.h"

#include <mutex>
#include <string_view>

double find_sample_freq(R3000::Sample const* sample,
                        std::mutex& mutex,
                        std::string_view name);
