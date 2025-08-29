#include "upse.h"

#include <fmt/format.h>

#include <QAbstractEventDispatcher>

#include <algorithm>
#include <cassert>

using namespace std::literals;
using namespace std::chrono;

namespace {

upse_iofuncs_t stdio_funcs{
    .open_impl = (void* (*)(const char* path, const char* mode))fopen,
    .read_impl = (size_t (*)(void* ptr, size_t size, size_t nmemb, void* file))fread,
    .seek_impl = (int (*)(void* file, long offset, int whence))fseek,
    .close_impl = (int (*)(void* file))fclose,
    .tell_impl = (long (*)(void* file))ftell,
};

constexpr auto SNAPSHOT_INTERVAL = 30s;

} // namespace

UpseModule::UpseModule(QObject* parent)
    : QThread{parent}
{
    this->moveToThread(this);
    _slow_timer.moveToThread(this);
    _fast_timer.moveToThread(this);

    _audio = open_sound_device(44100);

    if (!_audio) {
        return;
    }

    QObject::connect(&_slow_timer, &QTimer::timeout, this, &UpseModule::slow_timer_fired);
    _slow_timer.setSingleShot(false);
    QObject::connect(&_fast_timer, &QTimer::timeout, this, &UpseModule::fast_timer_fired);
    _fast_timer.setSingleShot(false);

    for (auto& channel : _control.input.channel) {
        channel.vol_multiplier = 1.f;
    }
}

void UpseModule::run()
{
    QThread::currentThread()->setObjectName("UpseModule");
    emit state_changed(_state);
    _slow_timer.start(500);

    int16_t* buf;
    int n, error;
    bool need_drain = false;

    while (!_shutdown) {
        if (_state == State::Seeking) {
            _control.input.speed_multiplier = 10;
            if (need_drain) {
                pa_simple_drain(_audio.get(), &error);
                need_drain = false;
            }
        }

        if (_mod && (_state == State::Playing || _state == State::Seeking)) {
            n = upse_eventloop_render(_mod.get(), &buf);

            auto const current_seek = milliseconds{upse_eventloop_tell_seek(_mod.get())};
            if (_snapshots.back().first + SNAPSHOT_INTERVAL < current_seek) {
                take_snapshot();
            }

            if (n == 0) {
                set_state(State::Stopped);
                _paused = true;
                _control.input.speed_multiplier = _speed;
                slow_timer_fired();
            }
        } else {
            n = 0;
        }

        if (n > 0 && buf) {
            pa_simple_write(_audio.get(), buf, n * 2 * sizeof(int16_t), &error);
            _control.input.speed_multiplier = _speed;
            need_drain = true;
            if (_state == State::Seeking) {
                set_state(_paused ? State::Paused : State::Playing);
                slow_timer_fired();
            }
        }

        if (need_drain && _state != State::Playing) {
            pa_simple_drain(_audio.get(), &error);
            need_drain = false;
        }

        eventDispatcher()->processEvents((_state == State::Seeking || _state == State::Playing)
                                             ? QEventLoop::AllEvents
                                             : QEventLoop::WaitForMoreEvents);
    }
}

void UpseModule::take_snapshot()
{
    using pair = decltype(_snapshots)::value_type;

    auto const current_seek = milliseconds{upse_eventloop_tell_seek(_mod.get())};
    _snapshots.emplace_back(pair{current_seek, {}});
    upse_module_take_snapshot(_mod.get(), &_snapshots.back().second);
}

void UpseModule::seek(int pos)
{
    if (!_mod) {
        return;
    }

    int const current_seek = upse_eventloop_tell_seek(_mod.get());

    if (pos == current_seek) {
        return;
    }

    auto it = std::ranges::upper_bound(_snapshots,
                                       milliseconds{pos},
                                       std::less<>{},
                                       [](auto const& element) { return element.first; });
    assert(it != _snapshots.begin());
    if (pos < current_seek || prev(it)->first.count() > current_seek) {
        upse_module_restore_snapshot(_mod.get(), &prev(it)->second);

        if (prev(it)->first.count() == pos) {
            return;
        }
    }

    upse_eventloop_seek(_mod.get(), pos);
    set_state(State::Seeking);
}

void UpseModule::load_file(QString const& file_name)
{
    _mod.reset(upse_module_open(file_name.toStdString().c_str(), &stdio_funcs, &_control));
    _snapshots.clear();
    _snapshots.shrink_to_fit();

    if (!_mod) {
        set_state(State::Unloaded);
        return;
    }

    _snapshots.reserve(
        (_mod->metadata->length / duration_cast<milliseconds>(SNAPSHOT_INTERVAL).count()) + 1);
    take_snapshot();

    _paused = false;
    set_state(State::Playing);
    emit total_time_changed(milliseconds{_mod->metadata->length});
    emit seek_changed(0ms);
}

void UpseModule::toggle_pause()
{
    if (!_mod) {
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
    if (!_mod) {
        return;
    }

    seek(0);
    set_state(State::Stopped);
    _paused = true;
}

void UpseModule::set_speed(float speed)
{
    _speed = speed;
    if (_state != State::Seeking) {
        _control.input.speed_multiplier = _speed;
    }
}

void UpseModule::mute_channel(int ch, bool muted) { _control.input.channel[ch].mute = muted; }

void UpseModule::set_channel_vol(int ch, float vol)
{
    _control.input.channel[ch].vol_multiplier = vol;
}

void UpseModule::shutdown()
{
    _slow_timer.stop();
    _fast_timer.stop();
    _shutdown = true;
}

void UpseModule::set_state(State new_state)
{
    if (new_state == State::Playing) {
        _fast_timer.start(0);
    } else {
        _fast_timer.stop();
    }

    auto const old_state = _state;
    _state = new_state;
    if (old_state != new_state) {
        state_changed(new_state);
    }
}

void UpseModule::slow_timer_fired()
{
    if (!_mod || _state == State::Seeking) {
        return;
    }
    emit seek_changed(milliseconds{upse_eventloop_tell_seek(_mod.get())});
}

void UpseModule::fast_timer_fired()
{
    auto const compute_rms = [](auto&& buf) {
        double rms = 0;
        for (auto s : buf) {
            rms += s * s;
        }
        return std::sqrt(rms / std::size(buf));
    };
    emit sound_level_changed(compute_rms(_control.output.window_l) / 32768,
                             compute_rms(_control.output.window_r) / 32768);
}
