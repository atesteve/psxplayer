// SPDX-FileCopyrightText: 2026 Aitor Esteve Alvarado
// SPDX-License-Identifier: GPL-3.0-only

#include "adpcm.h"
#include "iop/util.h"

#include <fftw3.h>
#include <fmt/format.h>

#include <algorithm>
#include <ranges>
#include <type_traits>
#include <cmath>
#include <iterator>
#include <complex>
#include <mutex>
#include <cstdlib>
#include <thread>
#include <vector>
#include <numeric>

namespace {

struct fftw_plan_deleter {
    static void operator()(fftw_plan p) noexcept { fftw_destroy_plan(p); }
};

using fftw_plan_ptr = std::unique_ptr<std::remove_pointer_t<fftw_plan>, fftw_plan_deleter>;

// Technically, bitfield layout is implementation-defined, so it shouldn't be relied upon. However,
// gcc, clang and msvc all implement the same layout, so I'm keeping this.
struct ADPCM_block {
    uint8_t shift : 4;
    uint8_t filter : 4;
    uint8_t loop_end : 1;
    uint8_t loop_repeat : 1;
    uint8_t loop_start : 1;
    uint8_t : 5;
    struct Data {
        int8_t lo_sample : 4;
        int8_t hi_sample : 4;
    } data[14];
};

static_assert(sizeof(ADPCM_block) == 16);
static_assert(alignof(ADPCM_block) == 1);

constexpr auto SAMPLES_PER_BLOCK = sizeof(ADPCM_block::data) * 2;

int32_t adpcm_filter(uint8_t filter, int32_t sample, int32_t a, int32_t b)
{
    // clang-format off
    switch (filter) {
        case 0: return sample;
        case 1: return sample + (60  * a          + 32) / 64;
        case 2: return sample + (115 * a - 52 * b + 32) / 64;
        case 3: return sample + (98  * a - 55 * b + 32) / 64;
        case 4: return sample + (122 * a - 60 * b + 32) / 64;
        // Invalid, just return the raw sample.
        default: return sample;
    }
    // clang-format on
}

std::pair<double, double> decode_adpcm_block(std::span<uint8_t const> ram,
                                             uint32_t addr,
                                             std::pair<double, double> prev_samples,
                                             std::span<double> out)
{
    if (addr > ram.size() - 16) {
        return {0, 0};
    }

    auto [a, b] = prev_samples;
    auto const* block = stdx::start_lifetime_as<ADPCM_block const>(&ram[addr]);

    for (auto const& [i, data] : std::ranges::enumerate_view{block->data}) {
        auto const process_sample = [&](int32_t sample) {
            auto const shifted = sample << (12 - block->shift);
            int32_t const ai32 = a * 32767;
            int32_t const bi32 = b * 32767;

            auto const ret = adpcm_filter(block->filter, shifted, ai32, bi32);
            auto const ret_double = std::clamp<int32_t>(ret, -32767, 32767) / 32767.0;

            b = std::exchange(a, ret_double);

            return ret_double;
        };

        auto const s1 = process_sample(data.lo_sample);
        auto const s2 = process_sample(data.hi_sample);

        if (!out.empty()) {
            out[i * 2] = s1;
            out[i * 2 + 1] = s2;
        }
    }

    return {a, b};
}

std::pair<double, double> prime_adpcm(std::span<uint8_t const> ram,
                                      uint32_t addr,
                                      uint32_t loop_addr,
                                      SampleBounds const& bounds)
{
    std::pair<double, double> ret{0, 0};

    while (addr != bounds.loop_addr && addr < ram.size()) {
        ret = decode_adpcm_block(ram, addr, ret, {});
        auto const* block = stdx::start_lifetime_as<ADPCM_block const>(&ram[addr]);
        if (block->loop_end && block->loop_repeat) {
            addr = loop_addr;
        } else {
            addr += sizeof(ADPCM_block);
        }
    }

    return ret;
}

double quinn_second_estimator(fftw_complex const& x0_in,
                              fftw_complex const& x1_in,
                              fftw_complex const& x2_in)
{
    std::complex<double> const x0{x0_in[0], x0_in[1]};
    std::complex<double> const x1{x1_in[0], x1_in[1]};
    std::complex<double> const x2{x2_in[0], x2_in[1]};

    auto const tau = [](double x) {
        static const double sqrt6 = sqrt(6);
        static const double sqrt2_3 = sqrt(2 / 3.0);
        return .25 * log(3 * x * x + 6 * x + 1)
            - sqrt6 / 24 * log((x + 1 - sqrt2_3) / (x + 1 + sqrt2_3));
    };

    // ap = (X[k + 1].r * X[k].r + X[k+1].i * X[k].i)  /  (X[k].r * X[k].r + X[k].i * X[k].i)
    double const ap = (x2.real() * x1.real() + x2.imag() * x1.imag())
        / (x1.real() * x1.real() + x1.imag() * x1.imag());

    // dp = -ap / (1 – ap)
    double const dp = -ap / (1 - ap);

    // am = (X[k – 1].r * X[k].r + X[k – 1].i * X[k].i)  /  (X[k].r * X[k].r + X[k].i * X[k].i)
    double am = (x0.real() * x1.real() + x0.imag() * x1.imag())
        / (x1.real() * x1.real() + x1.imag() * x1.imag());

    // dm = am / (1 – am)
    double dm = am / (1 - am);

    // d = (dp + dm) / 2 + tau(dp * dp) – tau(dm * dm)
    double const d = (dp + dm) / 2 + tau(dp * dp) - tau(dm * dm);

    return d;
}

double abs(fftw_complex const& a)
{
    return std::sqrt(a[0] * a[0] + a[1] * a[1]);
}

[[maybe_unused]]
void plot(FFTW3Holder<fftw_complex> const& fft,
          double bucket_size,
          std::vector<std::pair<double, double>> const& regions,
          std::pair<double, double> freq,
          std::string_view name)
{
    std::stringstream command;
    command << "echo -en \"set title '" << name << "';"
            << "set datafile separator ',';";

    for (auto const& [i, region] : std::views::enumerate(regions)) {
        command << "set object " << (i + 1) << " rect from first " << region.first
                << ", 0 to first " << region.second
                << ", 3000 behind fc rgb '#afffff' fillstyle solid 1.00 noborder;";
        if (i > 5) {
            break;
        }
    }

    command << "plot '-' using 1:2 with lines, '' using 1:2;"
               "pause mouse close;";
    for (auto const& [i, c] : std::views::enumerate(fft)) {
        command << "\\n" << (i * bucket_size) << "," << abs(c);
    }
    command << "\\ne";
    command << "\\n" << freq.first << ',' << freq.second << "\\ne";
    command << "\" | gnuplot -p";
    // fmt::print("{}\n", command.str());
    std::thread t{[command = command.str()] { std::system(command.c_str()); }};
    t.detach();
}

double find_peak_freq(FFTW3Holder<fftw_complex> const& fft, [[maybe_unused]] std::string_view name)
{
    static constexpr double MAX_FREQ = 22050;

    double const bucket_size = MAX_FREQ / (fft.size() - 1);

    auto const max_element = [&](auto&& start, auto&& end) {
        auto const max_index = std::max_element(
            fft.begin() + start,
            fft.begin() + end,
            [](fftw_complex const& a, fftw_complex const& b) { return abs(a) < abs(b); });
        return std::make_pair(size_t(std::distance(fft.begin(), max_index)), abs(*max_index));
    };

    auto const max_value = max_element(0, fft.size()).second;

    static constexpr auto C1 = 32.703195662;

    double max_sum = 0;
    std::pair<size_t, size_t> max_sum_region = {0, 0};
    std::vector<std::pair<double, double>> max_regions;

    for (auto i = 0u; i < (84 * 8); ++i) {
        auto const base_freq = C1 * std::pow(2, i / (12.0 * 8));
        int const n_regions = int(22050 / base_freq) - 1;
        int const half_width = std::max(1.0, (fft.size() * 0.1) / n_regions);

        double sum = 0;
        int n_samples = 0;
        std::vector<std::pair<double, double>> regions;

        for (auto j = 0; j < n_regions; ++j) {
            int const index = (base_freq * (j + 1)) / bucket_size;
            int const start_index = std::max(0, index - half_width);
            int const end_index = std::min<int>(fft.size(), index + half_width);

            if (j == 0) {
                auto const [index, value] = max_element(start_index, end_index);
                if (value < max_value * 0.05) {
                    n_samples = 1;
                    break;
                }
            }

            regions.push_back({start_index * bucket_size, end_index * bucket_size});

            n_samples += end_index - start_index;
            sum += std::transform_reduce(fft.begin() + start_index,
                                         fft.begin() + end_index,
                                         0.0,
                                         std::plus<>{},
                                         [](auto&& n) { return abs(n); });
        }

        sum /= n_samples;

        if (sum > max_sum) {
            max_sum = sum;
            int const index = base_freq / bucket_size;
            max_sum_region = {index - half_width, index + half_width};
            max_regions = std::move(regions);
        }
    }

    auto const [index, value] = max_element(max_sum_region.first, max_sum_region.second);

    if (index == 0 || index >= fft.size()) {
        return 0;
    }

    auto const freq =
        index * bucket_size + quinn_second_estimator(fft[index - 1], fft[index], fft[index + 1]);

    // fmt::print("{}: {},{}\n", name, freq, abs(*max_region_index));
    // plot(fft, bucket_size, max_regions, {freq, value}, name);

    return freq;
}

} // namespace

void fftw_deleter::operator()(void* p) noexcept
{
    fftw_free(p);
}

template<>
FFTW3Holder<double>::FFTW3Holder(size_t size)
    : _fftw{fftw_alloc_real(size)}
    , _size{size}
{}

template<>
FFTW3Holder<fftw_complex>::FFTW3Holder(size_t size)
    : _fftw{fftw_alloc_complex(size)}
    , _size{size}
{}

SampleBounds get_sample_bounds(std::span<uint8_t const> ram, uint32_t addr, uint32_t loop_addr)
{
    uint32_t const start_addr = addr;
    uint32_t max_addr = addr;
    bool jump_taken = false;

    while (addr <= ram.size() - 16) {
        auto const* block = stdx::start_lifetime_as<ADPCM_block const>(&ram[addr]);

        if (block->loop_start) {
            loop_addr = addr;
        }

        addr += sizeof(ADPCM_block);
        max_addr = std::max(addr, max_addr);

        if (block->loop_end && block->loop_repeat) {
            if (jump_taken || (loop_addr >= start_addr && loop_addr < addr)) {
                return {
                    .start_addr = start_addr,
                    .loop_addr = loop_addr,
                    .end_addr = addr,
                    .max_addr = max_addr,
                };
            }
            addr = loop_addr;
            jump_taken = true;

        } else if (block->loop_end) {
            return {
                .start_addr = start_addr,
                .loop_addr = start_addr,
                .end_addr = addr,
                .max_addr = max_addr,
            };
        }
    }

    return {};
}

std::optional<Sample> decode_adpcm_sample(std::span<uint8_t const> ram,
                                          uint32_t addr,
                                          uint32_t loop_addr,
                                          std::optional<SampleBounds> bounds_in,
                                          int repeats,
                                          Sample* out_in)
{
    auto const& bounds = bounds_in ? *bounds_in : get_sample_bounds(ram, addr, loop_addr);

    if (bounds.end_addr == 0) {
        return std::nullopt;
    }

    auto const blocks = (bounds.end_addr - bounds.loop_addr) / sizeof(ADPCM_block);

    std::optional<Sample> local_sample;
    auto& out = [&] -> Sample& {
        if (out_in) {
            return *out_in;
        } else {
            local_sample.emplace(blocks * repeats * SAMPLES_PER_BLOCK);
            return *local_sample;
        }
    }();

    auto prev_samples = prime_adpcm(ram, addr, loop_addr, bounds);

    for (auto i = 0u; i < blocks * repeats; ++i) {
        uint32_t p = bounds.loop_addr + (i % blocks) * sizeof(ADPCM_block);
        uint32_t out_p = i * SAMPLES_PER_BLOCK;

        prev_samples = decode_adpcm_block(
            ram, p, prev_samples, std::span{out}.subspan(out_p, SAMPLES_PER_BLOCK));
    }

    if (out_in) {
        return std::nullopt;
    } else {
        return {std::move(out)};
    }
}

double find_sample_freq(std::span<uint8_t const> ram,
                        uint32_t addr,
                        uint32_t loop_addr,
                        SampleBounds const& bounds,
                        std::mutex& mutex,
                        std::string_view name)
{
    auto const blocks = (bounds.end_addr - bounds.loop_addr) / sizeof(ADPCM_block);
    int repeats = 1;
    if (blocks < 200) {
        repeats = 399 / blocks;
    }

    Sample sample{blocks * repeats * SAMPLES_PER_BLOCK};
    FFTW3Holder<fftw_complex> fft{(blocks * repeats * SAMPLES_PER_BLOCK) / 2 + 1};

    std::unique_lock lock{mutex};
    fftw_plan_ptr plan{
        fftw_plan_dft_r2c_1d(sample.size(), sample.data(), fft.data(), FFTW_ESTIMATE)};
    lock.unlock();

    decode_adpcm_sample(ram, addr, loop_addr, bounds, repeats, &sample);

    fftw_execute(plan.get());

    return find_peak_freq(fft, name);
}
