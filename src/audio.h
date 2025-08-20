#pragma once

#include <pulse/simple.h>

#include <memory>

struct pa_simple_deleter {
    void operator()(pa_simple* pa) const noexcept { pa_simple_free(pa); }
};

using pa_simple_unique_ptr = std::unique_ptr<pa_simple, pa_simple_deleter>;

pa_simple_unique_ptr open_sound_device(unsigned rate);
