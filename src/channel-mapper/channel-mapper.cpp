// SPDX-FileCopyrightText: 2026 Aitor Esteve Alvarado
// SPDX-License-Identifier: GPL-3.0-only

#include "channel-mapper.h"
#include "square-mapper.h"
#include "square-retro-mapper.h"
#include "null-mapper.h"

#include <unordered_map>

namespace {

using cm_ptr = std::unique_ptr<ChannelMapper>;
using make_mapper_fn = cm_ptr (*)(upse_module_instance_t* ins, std::string_view title);

const std::unordered_map<std::string_view, make_mapper_fn> builder_map{
    {
        "Final Fantasy 7",
        [](auto*, auto title) -> cm_ptr {
            if (title == "World Crisis" || title == "Staff Roll") {
                return std::make_unique<NullMapper>(24);
            } else {
                return std::make_unique<NullMapper>(16);
            }
        },
    },
    {
        "Final Fantasy 8",
        [](auto*, auto) -> cm_ptr {
            return std::make_unique<SquareMapper>(0x80015fe0, 0xf4, 0x110);
        },
    },
    {
        "Final Fantasy 9",
        [](auto*, auto) -> cm_ptr {
            return std::make_unique<SquareMapper>(0x800585e0, 0x118, 0x134);
        },
    },
    {
        "Chrono Cross",
        [](auto*, auto) -> cm_ptr {
            return std::make_unique<SquareMapper>(0x8004d084, 0x108, 0x124);
        },
    },
    {
        "Final Fantasy VI",
        [](auto* ins, auto) -> cm_ptr {
            return std::make_unique<SquareRetroMapper>(ins, 0x80150000);
        },
    },
    {
        "Chrono Trigger",
        [](auto* ins, auto) -> cm_ptr {
            return std::make_unique<SquareRetroMapper>(ins, 0x80170000);
        },
    },
};
} // namespace

std::unique_ptr<ChannelMapper> ChannelMapper::build(upse_module_instance_t* ins,
                                                    std::string_view game_name,
                                                    std::string_view title)
{
    auto const it = builder_map.find(game_name);
    if (it == builder_map.end()) {
        return std::make_unique<NullMapper>(24);
    }
    return it->second(ins, title);
}
