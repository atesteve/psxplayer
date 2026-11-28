// SPDX-FileCopyrightText: 2026 Aitor Esteve Alvarado
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "audio.h"
#include "psx-player.h"

#include <QThread>
#include <QTimer>

#include <memory>
#include <chrono>
#include <vector>

class UpseModule : public QThread {
    Q_OBJECT

public:
    enum class State {
        Unloaded,
        Paused,
        Playing,
        Stopped,
        Seeking,
    };

    explicit UpseModule(QObject* parent = nullptr);
    ~UpseModule() = default;

    void run() override;

    operator bool() const { return (bool)_player; }

signals:
    void total_time_changed(std::chrono::milliseconds ms);
    void seek_changed(std::chrono::milliseconds ms);
    void state_changed(State state);
    void sound_level_changed(float l, float r);
    void channel_sound_level_changed(int ch, float l, float r);
    void channel_frequency_changed(int ch, double freq);
    void channel_fired(int ch);
    void supported_channels(int n_channels);

public slots:
    void seek(int pos);
    void toggle_pause();
    void stop();
    void shutdown();
    void load_file(QString const& file_name);
    void set_speed(float speed);
    void mute_channel(int ch, bool muted);
    void set_channel_vol(int ch, float vol);
    void set_endless_play(bool nonstop);

private slots:
    void slow_timer_fired();
    void fast_timer_fired();

private:
    void take_snapshot();
    void set_state(State new_state);

    std::unique_ptr<PsxPlayer> _player;
    Audio _audio;

    State _state{};
    bool _paused{};
    bool _shutdown{};
    float _speed{1};
    int _stopped_cycles{};
    QTimer _slow_timer;
    QTimer _fast_timer;

    std::vector<std::pair<std::chrono::milliseconds, SnapshotBuffer>> _snapshots;
};
