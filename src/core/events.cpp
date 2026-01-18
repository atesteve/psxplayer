#include "events.h"

#include <cstdint>
#include <vector>
#include <algorithm>
#include <ranges>

namespace {

struct EventTime {
    uint64_t clk_time;
    Timing::Handler handler;
};

bool event_time_greater(EventTime const& a, EventTime const& b)
{
    return a.clk_time > b.clk_time;
}

void push_heap(std::vector<EventTime>& v, EventTime e)
{
    v.emplace_back(std::move(e));
    std::ranges::push_heap(v, event_time_greater);
}

auto pop_heap(std::vector<EventTime>& v)
{
    std::ranges::pop_heap(v, event_time_greater);
    auto ret = std::move(v.back());
    v.pop_back();
    return ret;
}

void reorder_heap_head(std::vector<EventTime>& v)
{
    std::ranges::pop_heap(v, event_time_greater);
    std::ranges::push_heap(v, event_time_greater);
}

} // namespace

struct Timing::Private {
    Timing::Handler schedule(Event event);
    void run_events();

    uint64_t clk{};
    std::underlying_type_t<Timing::Handler> next_handler{};
    std::unordered_map<Timing::Handler, Timing::Event> events;
    std::vector<EventTime> event_heap;
};

Timing::Handler Timing::Private::schedule(Event event)
{
    if (event.phase == Event::UNINITIALIZED) {
        event.phase = event.period;
    }
    auto const handler = Timing::Handler{next_handler++};
    auto const [it, _] = events.emplace(handler, std::move(event));
    push_heap(event_heap,
              {
                  .clk_time = clk + it->second.phase,
                  .handler = handler,
              });
    return handler;
}

void Timing::Private::run_events()
{
    auto const current_clk = clk;
    while (event_heap.front().clk_time <= current_clk) {
        auto& event_time = event_heap.front();
        auto const it = events.find(event_time.handler);
        auto& [_, event] = *it;
        event.callback();
        if (event.type == Event::Type::ONE_SHOT) {
            pop_heap(event_heap);
            events.erase(it);
            continue;
        }
        event_time.clk_time += event.period;
        reorder_heap_head(event_heap);
    }
}

Timing::Handler Timing::schedule(Event event)
{
    return _p->schedule(std::move(event));
}

void Timing::advance_clock(uint64_t cycles)
{
    _p->clk += cycles;
}

uint64_t Timing::get_clock() const
{
    return _p->clk;
}

void Timing::run_events()
{
    _p->run_events();
}

void Timing::init(R3000* emu)
{}

Timing::Timing()
    : _p{std::make_unique<Private>()}
{}

Timing::~Timing() = default;
