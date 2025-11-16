#pragma once

#include <QAudioSink>
#include <QMediaDevices>

#include <cstdint>
#include <span>

class Audio : public QObject {
    Q_OBJECT
public:
    explicit Audio(QThread* t, QObject* o = nullptr);

    void write(std::span<int16_t const> buffer);
    void start();
    void stop();

private:
    QMediaDevices _mediaDevices;
    std::unique_ptr<QAudioSink> _sink;
    QIODevice* _audio{};
    bool _running{};
};
