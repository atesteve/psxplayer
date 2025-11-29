#include "audio.h"

#include <SDL3/SDL_audio.h>
#include <SDL3/SDL_events.h>

#include <fmt/format.h>

#include <thread>

using namespace std::literals;

void Audio::SDL_AudioStreamDeleter::operator()(SDL_AudioStream* ptr) noexcept
{
    SDL_DestroyAudioStream(ptr);
}

Audio::Audio()
{
    SDL_AudioSpec const spec{
        .format = SDL_AUDIO_S16LE,
        .channels = 2,
        .freq = 44100,
    };
    _stream.reset(
        SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr));
    if (!_stream) {
        fmt::println("{}", SDL_GetError());
    }
}

void Audio::start()
{
    _running = true;

    if (!_stream) {
        return;
    }
    SDL_ResumeAudioStreamDevice(_stream.get());
}

void Audio::stop()
{
    _running = false;

    if (!_stream) {
        return;
    }
    SDL_FlushAudioStream(_stream.get());
    SDL_PauseAudioStreamDevice(_stream.get());
}

bool Audio::write(std::span<int16_t const> buffer)
{
    static constexpr int MAX_SAMPLES = 8820; // 50ms
    static constexpr int MAX_RETRIES = 100;

    if (!_stream) {
        return false;
    }

    int const bytes_to_write = buffer.size() * sizeof(int16_t);
    int retries = 0;

    while (retries < MAX_RETRIES) {
        retries++;
        auto const nqueued = SDL_GetAudioStreamQueued(_stream.get());
        if (nqueued == -1) {
            fmt::println("{}", SDL_GetError());
            return false;
        }
        if (MAX_SAMPLES - nqueued > bytes_to_write) {
            SDL_PutAudioStreamData(_stream.get(), buffer.data(), buffer.size_bytes());
            return true;
        } else {
            std::this_thread::sleep_for(5ms);
        }
    }

    return false;
}
