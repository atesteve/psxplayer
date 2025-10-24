#include "adpcm.h"
#include "iop/util.h"

#include <fftw3.h>

#include <algorithm>
#include <ranges>
#include <type_traits>
#include <cmath>
#include <iterator>
#include <complex>
#include <mutex>

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
    default:
        // Invalid, just return the raw sample.
        return sample;
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

std::pair<double, double>
    prime_adpcm(std::span<uint8_t const> ram, uint32_t addr, uint32_t repeat_addr)
{
    if (addr > ram.size() - 16 || repeat_addr >= ram.size() || repeat_addr <= addr) {
        return {0, 0};
    }

    std::pair<double, double> ret{0, 0};

    for (; addr < repeat_addr; addr += sizeof(ADPCM_block)) {
        ret = decode_adpcm_block(ram, addr, ret, {});
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

double find_peak_freq(FFTW3Holder<fftw_complex> const& fft)
{
    static constexpr double MAX_FREQ = 22050;
    static constexpr double SEARCH_MIN_FREQ = 50;
    static constexpr double SEARCH_MAX_FREQ = 1500;

    double const bucket_size = MAX_FREQ / (fft.size() - 1);
    auto const start_index = [&] {
        unsigned ret = SEARCH_MIN_FREQ / bucket_size;
        return ret == 0 ? 1 : ret;
    }();
    auto const end_index = [&] {
        unsigned ret = SEARCH_MAX_FREQ / bucket_size;
        return ret <= start_index ? start_index + 1 : ret;
    }();

    auto const it = std::max_element(
        fft.begin() + start_index,
        fft.begin() + end_index,
        [](fftw_complex const& a, fftw_complex const& b) { return abs(a) < abs(b); });
    auto const index = std::distance(fft.begin(), it);
    auto const d = quinn_second_estimator(*std::prev(it), *it, *std::next(it));

    auto const freq = (index + d) * bucket_size;

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

std::pair<uint32_t, uint32_t>
    get_sample_bounds(std::span<uint8_t const> ram, uint32_t addr, uint32_t loop_addr)
{
    uint32_t const start = addr;
    uint32_t flag_loop_addr = 0;
    while (addr <= ram.size() - 16) {
        auto const* block = stdx::start_lifetime_as<ADPCM_block const>(&ram[addr]);

        if (block->loop_start) {
            flag_loop_addr = addr;
        }

        addr += sizeof(ADPCM_block);

        if (block->loop_end) {
            if (flag_loop_addr != 0) {
                return {flag_loop_addr, addr};
            }
            if (loop_addr >= start && loop_addr < addr) {
                return {loop_addr, addr};
            }
            return {start, addr};
        }
    }

    return {0, 0};
}

std::optional<Sample> decode_adpcm_sample(std::span<uint8_t const> ram,
                                          uint32_t addr,
                                          uint32_t loop_addr_in,
                                          uint32_t sample_end_addr,
                                          int repeats,
                                          Sample* out_in)
{
    auto const [loop_addr, sample_end] = [&] {
        if (sample_end_addr) {
            return std::pair{loop_addr_in, sample_end_addr};
        }
        return get_sample_bounds(ram, addr, loop_addr_in);
    }();

    if (sample_end == 0) {
        return std::nullopt;
    }

    auto const blocks = (sample_end - loop_addr) / sizeof(ADPCM_block);

    std::optional<Sample> local_sample;
    auto& out = [&] -> Sample& {
        if (out_in) {
            return *out_in;
        } else {
            local_sample.emplace(blocks * repeats * SAMPLES_PER_BLOCK);
            return *local_sample;
        }
    }();

    auto prev_samples = prime_adpcm(ram, addr, loop_addr);

    auto const decode_pass = [&] {
        for (auto i = 0u; i < blocks * repeats; ++i) {
            uint32_t p = loop_addr + (i % blocks) * sizeof(ADPCM_block);
            uint32_t out_p = i * SAMPLES_PER_BLOCK;

            prev_samples = decode_adpcm_block(
                ram, p, prev_samples, std::span{out}.subspan(out_p, SAMPLES_PER_BLOCK));
        }
    };

    // Do two passes. Technically, the generated waveform is not the same after one loop, so
    // hopefully by running two passes the resulting waveform loops better.
    decode_pass();
    decode_pass();

    if (out_in) {
        return std::nullopt;
    } else {
        return {std::move(out)};
    }
}

double find_sample_freq(std::span<uint8_t const> ram,
                        uint32_t addr,
                        uint32_t loop_addr,
                        uint32_t sample_end_addr,
                        std::mutex& mutex)
{
    auto const blocks = (sample_end_addr - loop_addr) / sizeof(ADPCM_block);
    int repeats = 1;
    if (blocks < 200) {
        repeats = 399 / blocks;
    }

    Sample sample{blocks * repeats * SAMPLES_PER_BLOCK};
    FFTW3Holder<fftw_complex> fft{(blocks * repeats * SAMPLES_PER_BLOCK) / 2 + 1};

    std::unique_lock lock{mutex};
    fftw_plan_ptr plan{fftw_plan_dft_r2c_1d(sample.size(), sample.data(), fft.data(), 0)};
    lock.unlock();

    decode_adpcm_sample(ram, addr, loop_addr, sample_end_addr, repeats, &sample);

    fftw_execute(plan.get());

    return find_peak_freq(fft);
}
