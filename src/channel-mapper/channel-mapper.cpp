#include "channel-mapper.h"
#include "square-mapper.h"
#include "square-retro-mapper.h"
#include "null-mapper.h"

#include <unordered_map>

namespace {

using cm_ptr = std::unique_ptr<ChannelMapper>;

const std::unordered_map<std::string_view, cm_ptr (*)(upse_module_instance_t* ins)> builder_map{
    {
        "Final Fantasy 7",
        [](auto) -> cm_ptr { return std::make_unique<NullMapper>(16); },
    },
    {
        "Final Fantasy 8",
        [](auto) -> cm_ptr { return std::make_unique<SquareMapper>(0x80015fe0, 0xf4, 0x110); },
    },
    {
        "Final Fantasy 9",
        [](auto) -> cm_ptr { return std::make_unique<SquareMapper>(0x800585e0, 0x118, 0x134); },
    },
    {
        "Chrono Cross",
        [](auto) -> cm_ptr { return std::make_unique<SquareMapper>(0x8004d084, 0x108, 0x124); },
    },
    {
        "Final Fantasy VI",
        [](auto ins) -> cm_ptr { return std::make_unique<SquareRetroMapper>(ins, 0x80150000); },
    },
    {
        "Chrono Trigger",
        [](auto ins) -> cm_ptr { return std::make_unique<SquareRetroMapper>(ins, 0x80170000); },
    },
};
} // namespace

std::unique_ptr<ChannelMapper> ChannelMapper::build(upse_module_instance_t* ins,
                                                    std::string_view game_name)
{
    auto const it = builder_map.find(game_name);
    if (it == builder_map.end()) {
        return std::make_unique<NullMapper>(24);
    }
    return it->second(ins);
}
