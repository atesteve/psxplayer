// SPDX-FileCopyrightText: 2026 Aitor Esteve Alvarado
// SPDX-License-Identifier: GPL-3.0-only

#include "upse.h"

#include <fmt/format.h>

#include <QAbstractEventDispatcher>

#include <algorithm>
#include <cassert>
#include <ranges>

using namespace std::literals;
using namespace std::chrono;

namespace {

constexpr auto SNAPSHOT_INTERVAL = 15s;

} // namespace

UpseModule::UpseModule(QObject* parent)
    : QThread{parent}
{
    this->moveToThread(this);
    _slow_timer.moveToThread(this);
    _fast_timer.moveToThread(this);

    QObject::connect(&_slow_timer, &QTimer::timeout, this, &UpseModule::slow_timer_fired);
    _slow_timer.setSingleShot(false);
    QObject::connect(&_fast_timer, &QTimer::timeout, this, &UpseModule::fast_timer_fired);
    _fast_timer.setSingleShot(false);
}

void UpseModule::run()
{
    QThread::currentThread()->setObjectName("UpseModule");
    emit state_changed(_state);
    _slow_timer.start(500);

    std::vector<int16_t> buf;
    int64_t n;
    buf.resize(4096);

    while (!_shutdown) {
        if (_player && (_state == State::Playing || _state == State::Seeking)) {

            n = _player->render_audio(buf, _state == State::Seeking ? 0 : buf.size() / 4);

            if (auto const current_seek = _player->tell_seek();
                current_seek < _player->get_duration()
                && _snapshots.back().first + SNAPSHOT_INTERVAL < current_seek) {
                take_snapshot();
            }

            if (n == -1) {
                set_state(State::Stopped);
                _paused = true;
                slow_timer_fired();
            }
        } else {
            n = 0;
        }

        if (n > 0) {
            auto const success = _audio.write({buf.begin(), size_t(n * 2)});
            if (!success) {
                toggle_pause();
            }
        } else if (_state == State::Seeking && _player->seek_done()) {
            set_state(_paused ? State::Paused : State::Playing);
            slow_timer_fired();
        }

        // QEventLoop::AllEvents returns immediately if there are no events to dispatch.
        // QEventLoop::WaitForMoreEvents blocks until there is at least one event, then dispatches
        // it and returns.
        eventDispatcher()->processEvents((_state == State::Seeking || _state == State::Playing)
                                             ? QEventLoop::AllEvents
                                             : QEventLoop::WaitForMoreEvents);
    }

    _slow_timer.stop();
    _fast_timer.stop();
}

void UpseModule::take_snapshot()
{
    auto const current_seek = _player->tell_seek();
    auto& emplaced = _snapshots.emplace_back();
    emplaced.first = current_seek;
    emplaced.second = _player->take_snapshot();
}

void UpseModule::seek(int pos)
{
    if (!_player) {
        return;
    }

    milliseconds const pos_ms{pos};
    auto const current_seek = _player->tell_seek();

    if (pos_ms == current_seek) {
        return;
    }

    auto it = std::ranges::upper_bound(
        _snapshots, pos_ms, std::less<>{}, [](auto const& element) { return element.first; });
    assert(it != _snapshots.begin());
    if (pos_ms < current_seek || prev(it)->first > current_seek) {
        _player->restore_snapshot(prev(it)->second);
    }

    _player->seek(pos_ms);
    set_state(State::Seeking);
}

void UpseModule::load_file(QString const& file_name)
{
    _audio.stop();

    _player.reset();
    _snapshots.clear();
    _snapshots.shrink_to_fit();

    auto new_player = PsxPlayer::build(file_name.toStdString());

    if (!new_player) {
        set_state(State::Unloaded);
        emit supported_channels(0);
        return;
    }

    _player = std::move(*new_player);
    _player->set_playback_speed(_speed);

    take_snapshot();

    _paused = false;

    emit supported_channels(_player->n_channels());

    set_state(State::Playing);
    emit total_time_changed(_player->get_duration());
    emit seek_changed(0ms);
}

void UpseModule::toggle_pause()
{
    if (!_player) {
        return;
    }

    switch (_state) {
    case State::Seeking:
    case State::Playing:
        _paused = true;
        set_state(State::Paused);
        break;

    case State::Stopped:
        seek(0);
        [[fallthrough]];

    case State::Paused:
        _paused = false;
        set_state(State::Playing);
        break;

    case State::Unloaded:
        // Do nothing
        break;
    }
}

void UpseModule::stop()
{
    if (!_player) {
        return;
    }

    seek(0);
    set_state(State::Stopped);
    _paused = true;
}

void UpseModule::set_speed(float speed)
{
    _speed = speed;
    if (_player) {
        _player->set_playback_speed(speed);
    }
}

void UpseModule::mute_channel(int ch, bool muted)
{
    if (_player) {
        _player->mute_channel(ch, muted);
    }
}

void UpseModule::set_channel_vol(int ch, float vol)
{
    //_channel_state[ch].vol = vol;
}

void UpseModule::set_endless_play(bool nonstop)
{
    //_control.input.endless_play = nonstop;
}

void UpseModule::shutdown()
{
    _shutdown = true;
}

void UpseModule::set_state(State new_state)
{
    if (new_state == State::Playing) {
        _audio.start();
        _fast_timer.start(33);
    } else {
        if (_state == State::Playing) {
            _audio.stop();
        }
        _stopped_cycles = 0;
    }

    auto const old_state = _state;
    _state = new_state;
    if (old_state != new_state) {
        state_changed(new_state);
    }
}

void UpseModule::slow_timer_fired()
{
    if (_state != State::Playing && _stopped_cycles > 2) {
        _fast_timer.stop();
    }
    _stopped_cycles++;

    if (!_player || _state == State::Seeking) {
        return;
    }
    emit seek_changed(_player->tell_seek());
}

void UpseModule::fast_timer_fired()
{
    auto const compute_rms = [](auto&& buf) -> double {
        if (std::size(buf) == 0) {
            return 0;
        }
        double rms = 0;
        for (auto s : buf) {
            rms += s * s;
        }
        return std::sqrt(rms / std::size(buf));
    };

    if (_state == State::Playing) {
        auto const window = _player->get_master_window();
        emit sound_level_changed(compute_rms(window.first) / 32768,
                                 compute_rms(window.second) / 32768);
    } else {
        emit sound_level_changed(0, 0);
    }

    for (auto const& [ch_i, ch] : std::views::enumerate(_player->get_channel_information())) {
        if (_state != State::Playing) {
            emit channel_sound_level_changed(ch_i, 0, 0);
            continue;
        }
        emit channel_sound_level_changed(
            ch_i, compute_rms(ch.samples.first) / 32768, compute_rms(ch.samples.second) / 32768);
        emit channel_frequency_changed(ch_i, ch.freq);
        if (ch.fired) {
            emit channel_fired(ch_i);
        }
    }
}
