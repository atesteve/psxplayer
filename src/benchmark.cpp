#include "psxemu/core/r3000.h"

#include <fmt/format.h>
#include <fmt/chrono.h>

#include <chrono>

void run_benchmark(std::filesystem::path path)
{
    auto psf = load_psf(path);
    auto emu = R3000::build();
    emu->init(*psf);
    std::vector<int16_t> buf;
    buf.resize(21168000 / 10);

    int const nruns = 10;
    auto const snapshot = emu->take_snapshot();
    std::chrono::steady_clock::duration dur{};

    auto const run = [&] {
        emu->restore_snapshot(snapshot);
        auto const start = std::chrono::steady_clock::now();
        emu->render_audio(buf, buf.size() / 4);
        auto const end = std::chrono::steady_clock::now();
        dur += end - start;
    };

    for (auto i = 0; i < nruns; i++) {
        run();
    }

    fmt::println("{}", std::chrono::duration_cast<std::chrono::milliseconds>(dur / nruns));
}
