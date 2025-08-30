#include "audio.h"

namespace {
constexpr auto BUF_SAMPLE_SIZE = 1024u;

constexpr size_t advance(size_t p, size_t n = 1) { return (p + n) % (BUF_SAMPLE_SIZE * 2); }

} // namespace

Audio::Audio() = default;

Audio::~Audio()
{
    if (_stream) {
        Pa_StopStream(_stream);
        Pa_CloseStream(_stream);
    }
}

void Audio::init()
{
    _buffer.reserve(BUF_SAMPLE_SIZE * 2);
    for (auto i = 0u; i < BUF_SAMPLE_SIZE * 2; ++i) {
        _buffer.push_back(0);
    }
}

void Audio::write(std::span<int16_t> samples)
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
            auto const status = _cv.wait_for(lock, 1s, [this, next_write_p = advance(write_p, 2)] {
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

int Audio::pa_stream_callback(int16_t* output, unsigned long frame_count)
{
    size_t current_write_p = _write_p;
    size_t read_p = _read_p;

    for (auto i = 0u; i < frame_count; ++i) {
        if (read_p == current_write_p) {
            current_write_p = _write_p;
        }
        if (read_p == current_write_p) {
            // There is no audio data, so write just silence.
            output[i * 2] = 0;
            output[i * 2 + 1] = 0;
            continue;
        }

        output[i * 2] = _buffer[read_p];
        output[i * 2 + 1] = _buffer[advance(read_p)];

        read_p = advance(read_p, 2);
    }

    _read_p = read_p;
    _cv.notify_all();

    return 0;
}

int Audio::pa_stream_callback(const void*,
                              void* output,
                              unsigned long frame_count,
                              const PaStreamCallbackTimeInfo*,
                              PaStreamCallbackFlags,
                              void* user_data)
{
    return static_cast<Audio*>(user_data)->pa_stream_callback((int16_t*)output, frame_count);
}

std::unique_ptr<Audio> Audio::open(int rate)
{
    std::unique_ptr<Audio> _ret{new Audio};

    PaError err;

    err = Pa_OpenDefaultStream(&_ret->_stream,
                               0, // No input channels
                               2, // Stereo output
                               paInt16,
                               rate,
                               256, // Samples per request
                               Audio::pa_stream_callback,
                               _ret.get());
    if (err != paNoError) {
        return nullptr;
    }

    err = Pa_StartStream(_ret->_stream);

    if (err != paNoError) {
        return nullptr;
    }

    _ret->init();

    return _ret;
}
