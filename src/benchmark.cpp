#include "psxemu/core/r3000.h"

#include <fmt/format.h>
#include <fmt/chrono.h>

#include <chrono>

using namespace std::literals;

void run_benchmark(std::filesystem::path path, std::chrono::milliseconds time, bool fast_forward)
{
    auto psf = load_psf(path);
    auto emu = R3000::build();
    emu->init(*psf);
    auto const samples = (time.count() * 338688) / 7680;
    std::vector<int16_t> buf;
    buf.resize(samples * 4);

    int const nruns = 10;
    auto const snapshot = emu->take_snapshot();
    std::chrono::steady_clock::duration dur{};

    auto const run = [&] {
        emu->restore_snapshot(snapshot);
        auto const start = std::chrono::steady_clock::now();
        if (fast_forward) {
            emu->fast_forward(time, 1h);
        } else {
            emu->render_audio(buf, samples);
        }
        auto const end = std::chrono::steady_clock::now();
        dur += end - start;
    };

    for (auto i = 0; i < nruns; i++) {
        run();
    }

    fmt::println("{}", std::chrono::duration_cast<std::chrono::milliseconds>(dur / nruns));
}
