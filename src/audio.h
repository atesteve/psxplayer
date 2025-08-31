#pragma once

#include <QAudioSink>
#include <QIODevice>

#include <memory>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <span>

class Audio : public QIODevice {
    Q_OBJECT

public:
    static std::unique_ptr<Audio> open(int rate);
    ~Audio() = default;

    void write(std::span<int16_t> samples);
    void flush();

    qint64 readData(char *data, qint64 maxlen) override;
    qint64 writeData(const char *, qint64) override { return 0; };
    qint64 bytesAvailable() const override;
    qint64 size() const override { return 0; }

private:
    explicit Audio();
    void init();

    template <typename T>
    void write_impl(T&& samples);

    std::vector<int16_t> _buffer;
    std::unique_ptr<QAudioSink> _sink;

    std::condition_variable _cv;
    std::mutex _mutex;
    std::atomic_size_t _read_p;
    std::atomic_size_t _write_p;
};
