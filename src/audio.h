#pragma once

#include <portaudio.h>

#include <memory>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <span>

class Audio {
public:
    static std::unique_ptr<Audio> open(int rate);
    ~Audio();

    void write(std::span<int16_t> samples);

private:
    explicit Audio();
    void init();

    int pa_stream_callback(int16_t* output, unsigned long frame_count);

    static int pa_stream_callback(const void*,
                                  void* output,
                                  unsigned long frame_count,
                                  const PaStreamCallbackTimeInfo* time_info,
                                  PaStreamCallbackFlags status_flags,
                                  void* user_data);

    std::vector<int16_t> _buffer;
    PaStream* _stream{};

    std::condition_variable _cv;
    std::mutex _mutex;
    std::atomic_size_t _read_p;
    std::atomic_size_t _write_p;
};
