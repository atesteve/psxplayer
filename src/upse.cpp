#include "upse.h"

#include <fmt/format.h>

#include <QAbstractEventDispatcher>

#include <algorithm>
#include <cassert>

using namespace std::literals;
using namespace std::chrono;

extern "C" {
float multiplier = 1;
}

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

    _audio = open_sound_device(44100);

    if (!_audio) {
        return;
    }

    QObject::connect(&_slow_timer, &QTimer::timeout, this, &UpseModule::slow_timer_fired);
    _slow_timer.setSingleShot(false);
}

void UpseModule::run()
{
    QThread::currentThread()->setObjectName("UpseModule");
    _slow_timer.start(500);

    int16_t* buf;
    int n, error;
    bool need_drain = false;

    while (!_shutdown) {
        if (_seeking) {
            multiplier = 10;
            if (need_drain) {
                pa_simple_drain(_audio.get(), &error);
                need_drain = false;
            }
        }

        if (_mod) {
            n = upse_eventloop_render(_mod.get(), &buf);

            auto const current_seek = milliseconds{upse_eventloop_tell_seek(_mod.get())};
            if (_snapshots.back().first + SNAPSHOT_INTERVAL < current_seek) {
                take_snapshot();
            }

            if (n == 0) {
                _seeking = false;
                multiplier = 1;
                slow_timer_fired();
            }
        } else {
            n = 0;
        }

        if (n > 0 && buf) {
            pa_simple_write(_audio.get(), buf, n * 2 * sizeof(int16_t), &error);
            multiplier = 1;
            need_drain = true;
            if (_seeking) {
                _seeking = false;
                slow_timer_fired();
            }
        }

        if (need_drain && (n == 0 || !buf || _paused)) {
            pa_simple_drain(_audio.get(), &error);
            need_drain = false;
        }

        eventDispatcher()->processEvents((!_seeking && (_paused || n == 0))
                                             ? QEventLoop::WaitForMoreEvents
                                             : QEventLoop::AllEvents);
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

    auto it = std::ranges::upper_bound(_snapshots,
                                       milliseconds{pos},
                                       std::less<>{},
                                       [](auto const& element) { return element.first; });
    assert(it != _snapshots.begin());
    if (pos < current_seek || prev(it)->first.count() > current_seek) {
        upse_module_restore_snapshot(_mod.get(), &std::prev(it)->second);
    }

    upse_eventloop_seek(_mod.get(), pos);
    _seeking = true;
}

void UpseModule::load_file(QString const& file_name)
{
    _mod.reset(upse_module_open(file_name.toStdString().c_str(), &stdio_funcs));
    _snapshots.clear();
    _snapshots.shrink_to_fit();

    if (!_mod) {
        return;
    }

    _snapshots.reserve(
        (_mod->metadata->length / duration_cast<milliseconds>(SNAPSHOT_INTERVAL).count()) + 10);
    take_snapshot();

    _paused = false;
    emit total_time_changed(milliseconds{_mod->metadata->length});
    emit seek_changed(0ms);
}

void UpseModule::toggle_pause() { _paused = !_paused; }

void UpseModule::pause(bool state) { _paused = state; }

void UpseModule::shutdown()
{
    _slow_timer.stop();
    _shutdown = true;
}

void UpseModule::slow_timer_fired()
{
    if (!_mod || _seeking) {
        return;
    }
    emit seek_changed(milliseconds{upse_eventloop_tell_seek(_mod.get())});
}
