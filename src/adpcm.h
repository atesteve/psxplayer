#pragma once

#include <cstdint>
#include <span>
#include <optional>
#include <memory>

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

struct SampleBounds {
    uint32_t start_addr;
    uint32_t loop_addr;
    uint32_t end_addr;
    uint32_t max_addr;
};

SampleBounds get_sample_bounds(std::span<uint8_t const> ram, uint32_t addr, uint32_t loop_addr);

std::optional<Sample> decode_adpcm_sample(std::span<uint8_t const> ram,
                                          uint32_t addr,
                                          uint32_t loop_addr,
                                          std::optional<SampleBounds> bounds = std::nullopt,
                                          int repeats = 1,
                                          Sample* out = nullptr);

double find_sample_freq(std::span<uint8_t const> ram,
                        uint32_t addr,
                        uint32_t loop_addr,
                        SampleBounds const& bounds,
                        std::mutex& mutex);
