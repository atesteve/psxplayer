#include "audio.h"

#include <fmt/format.h>

#include <thread>

using namespace std::literals;

Audio::Audio(QThread* t, QObject* o)
    : QObject{o}
{
    moveToThread(t);

    auto const refresh_media_device = [this] {
        QAudioFormat format{};
        format.setSampleRate(44100);
        format.setChannelCount(2);
        format.setSampleFormat(QAudioFormat::Int16);

        QAudioDevice info{_mediaDevices.defaultAudioOutput()};

        if (!info.isFormatSupported(format)) {
            return;
        }

        _audio = nullptr;

        if (_sink && _running) {
            _sink->reset();
        }

        _sink = std::make_unique<QAudioSink>(format);

        if (_running) {
            _audio = _sink->start();
        }
    };

    refresh_media_device();
    QObject::connect(
        &_mediaDevices, &QMediaDevices::audioOutputsChanged, this, refresh_media_device);
}

void Audio::start()
{
    _running = true;

    if (!_sink) {
        return;
    }
    _audio = _sink->start();
}

void Audio::stop()
{
    _running = false;

    if (!_sink) {
        return;
    }
    _sink->reset();
    _audio = nullptr;
}

void Audio::write(std::span<int16_t const> buffer)
{
    static constexpr auto MAX_RETRIES = 10;
    if (!_audio) {
        return;
    }
    size_t bytes_written = 0;
    size_t const bytes_to_write = buffer.size() * sizeof(int16_t);
    int retries = 0;
    while (bytes_written < bytes_to_write && retries < MAX_RETRIES) {
        if (retries != 0) {
            std::this_thread::sleep_for(5ms);
        }
        auto const actually_written =
            _audio->write((char const*)&buffer[0] + bytes_written, bytes_to_write - bytes_written);
        if (actually_written < 0) {
            break;
        }
        if (actually_written == 0) {
            retries++;
        } else {
            retries = 1;
        }
        bytes_written += actually_written;
    }
}
