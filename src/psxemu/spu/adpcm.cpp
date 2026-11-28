// SPDX-FileCopyrightText: 2026 Aitor Esteve Alvarado
// SPDX-License-Identifier: GPL-3.0-only

#include "adpcm.h"

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

struct fftw_deleter {
    static void operator()(void* p) noexcept;
};

template<typename T>
class FFTW3Holder {
    using fftw_ptr = std::unique_ptr<T[], fftw_deleter>;

public:
    explicit FFTW3Holder(size_t size);

    T& operator[](size_t i) { return _fftw.get()[i]; }
    T const& operator[](size_t i) const { return _fftw.get()[i]; }

    auto begin(this auto&& self) { return &self[0]; }
    T const* cbegin() const { return begin(); }

    auto end(this auto&& self) { return &self[0] + self._size; }
    T const* cend() const { return end(); }

    auto data(this auto&& self) { return self.begin(); }

    size_t size() const { return _size; }

private:
    fftw_ptr _fftw;
    size_t _size;
};

using Sample = FFTW3Holder<double>;

namespace {

struct fftw_plan_deleter {
    static void operator()(fftw_plan p) noexcept { fftw_destroy_plan(p); }
};

using fftw_plan_ptr = std::unique_ptr<std::remove_pointer_t<fftw_plan>, fftw_plan_deleter>;

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

[[maybe_unused]]
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

double find_sample_freq(R3000::Sample const* sample, std::mutex& mutex, std::string_view name)
{
    static constexpr size_t MIN_SAMPLES = 5000;

    auto const [sample_loop, repeats] = [&] -> std::pair<std::span<int16_t const>, size_t> {
        if (sample->loop == std::numeric_limits<size_t>::max()) {
            return {std::span{sample->samples}, 1};
        } else {
            auto const sample_loop = std::span{sample->samples}.subspan(sample->loop);
            size_t const repeats = MIN_SAMPLES / (sample_loop.size()) + 1;
            return {sample_loop, repeats};
        }
    }();

    if (sample_loop.size() * repeats < MIN_SAMPLES) {
        return 0;
    }

    Sample sample_loop_d{sample_loop.size() * repeats};
    for (size_t i = 0; i < sample_loop_d.size(); i++) {
        sample_loop_d[i] = sample_loop[i % sample_loop.size()] / 32768.0;
    }

    FFTW3Holder<fftw_complex> fft{sample_loop_d.size() / 2 + 1};

    std::unique_lock lock{mutex};
    fftw_plan_ptr plan{fftw_plan_dft_r2c_1d(
        sample_loop_d.size(), sample_loop_d.data(), fft.data(), FFTW_ESTIMATE)};
    lock.unlock();

    fftw_execute(plan.get());

    return find_peak_freq(fft, name);
}
