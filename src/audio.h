#pragma once

#include <cstdint>
#include <span>
#include <memory>

struct SDL_AudioStream;

class Audio {
public:
    explicit Audio();

    bool write(std::span<int16_t const> buffer);
    void start();
    void stop();

private:
    struct SDL_AudioStreamDeleter {
        static void operator()(SDL_AudioStream*) noexcept;
    };
    std::unique_ptr<SDL_AudioStream, SDL_AudioStreamDeleter> _stream;
};
