#include "audio.h"

#include <QMediaDevices>

#include <generator>

namespace {
constexpr auto BUF_SAMPLE_SIZE = 1024u;

constexpr size_t advance(size_t p, size_t n = 1) { return (p + n) % (BUF_SAMPLE_SIZE * 2); }

} // namespace

Audio::Audio() = default;

void Audio::init()
{
    _buffer.resize(BUF_SAMPLE_SIZE * 2);
    setOpenMode(QIODevice::OpenModeFlag::ReadOnly);
    _sink->start(this);
}

template<typename T>
void Audio::write_impl(T&& samples)
{
    using namespace std::chrono_literals;

    size_t current_read_p = _read_p;
    size_t write_p = _write_p;

    size_t sample1, sample2;
    int nsamples = 0;

    for (auto const sample : samples) {
        if (nsamples == 0) {
            sample1 = sample;
            nsamples = 1;
            continue;
        }

        sample2 = sample;
        nsamples = 0;

        if (advance(write_p, 2) == current_read_p) {
            _write_p = write_p;

            std::unique_lock lock{_mutex};
            auto const status =
                _cv.wait_for(lock, 100ms, [this, next_write_p = advance(write_p, 2)] {
                    return _read_p != next_write_p;
                });

            if (!status) {
                _write_p = write_p;
                return; // Bail out
            }

            current_read_p = _read_p;
        }

        _buffer[write_p] = sample1;
        _buffer[advance(write_p)] = sample2;
        write_p = advance(write_p, 2);
    }

    _write_p = write_p;
}

void Audio::write(std::span<int16_t> samples) { return write_impl(samples); }

void Audio::flush()
{
    // Insert silence to force any remaining data to be flushed.
    auto const silence_generator = [this]() -> std::generator<int16_t> {
        for (auto i = 0u; i < _buffer.size(); ++i) {
            co_yield 0;
        }
    };
    return write_impl(silence_generator());
}

qint64 Audio::readData(char* output, qint64 maxlen)
{
    size_t current_write_p = _write_p;
    size_t read_p = _read_p;
    qint64 written = 0;
    auto* const output_s16 = reinterpret_cast<int16_t*>(output);

    for (auto i = 0u; i < maxlen / 2; ++i) {
        if (read_p == current_write_p) {
            current_write_p = _write_p;
        }
        if (read_p == current_write_p) {
            break;
        }

        output_s16[i * 2] = _buffer[read_p];
        output_s16[i * 2 + 1] = _buffer[advance(read_p)];

        read_p = advance(read_p, 2);
        written += 4;

        _read_p = read_p;
        _cv.notify_all();
    }

    _read_p = read_p;
    _cv.notify_all();

    return written;
}

qint64 Audio::bytesAvailable() const
{
    size_t const current_write_p = _write_p;
    size_t const current_read_p = _read_p;
    static constexpr auto element_size = sizeof(decltype(_buffer)::value_type);

    if (current_write_p >= current_read_p) {
        return (current_write_p - current_read_p) * element_size;
    }

    return (current_write_p + _buffer.size() - current_read_p) * element_size;
}

std::unique_ptr<Audio> Audio::open(int rate)
{
    std::unique_ptr<Audio> _ret{new Audio};

    QAudioFormat format{};
    format.setSampleRate(rate);
    format.setChannelCount(2);
    format.setSampleFormat(QAudioFormat::Int16);

    QAudioDevice info{QMediaDevices::defaultAudioOutput()};

    if (!info.isFormatSupported(format)) {
        return nullptr;
    }

    _ret->_sink = std::make_unique<QAudioSink>(format, _ret.get());
    _ret->init();

    return _ret;
}
