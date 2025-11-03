#include "channel-mapper.h"
#include "square-mapper.h"
#include "null-mapper.h"

#include <unordered_map>
#include <functional>

namespace {

const std::unordered_map<std::string_view, std::function<std::unique_ptr<ChannelMapper>()>>
    builder_map{
        {
            "Final Fantasy 7",
            [] { return std::make_unique<NullMapper>(16); },
        },
        {
            "Final Fantasy 8",
            [] { return std::make_unique<SquareMapper>(0x80015fe0, 0xf4, 0x110); },
        },
        {
            "Final Fantasy 9",
            [] { return std::make_unique<SquareMapper>(0x800585e0, 0x118, 0x134); },
        },
        {
            "Chrono Cross",
            [] { return std::make_unique<SquareMapper>(0x8004d084, 0x108, 0x124); },
        },
    };
} // namespace

std::unique_ptr<ChannelMapper> ChannelMapper::build(std::string_view game_name)
{
    auto const it = builder_map.find(game_name);
    if (it == builder_map.end()) {
        return std::make_unique<NullMapper>(24);
    }
    return it->second();
}
