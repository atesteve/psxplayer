#include "upse.h"

#include <fmt/format.h>

#include <QAbstractEventDispatcher>

using namespace std::literals;

namespace {

upse_iofuncs_t stdio_funcs{
    .open_impl = (void* (*)(const char* path, const char* mode))fopen,
    .read_impl = (size_t (*)(void* ptr, size_t size, size_t nmemb, void* file))fread,
    .seek_impl = (int (*)(void* file, long offset, int whence))fseek,
    .close_impl = (int (*)(void* file))fclose,
    .tell_impl = (long (*)(void* file))ftell,
};

struct upse_snapshot_deleter {
    static void operator()(upse_snapshot_t* snapshot) noexcept
    {
        upse_module_destroy_snapshot(snapshot);
    }
};

using upse_snapshot_ptr = std::unique_ptr<upse_snapshot_t, upse_snapshot_deleter>;

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
        if (_mod) {
            n = upse_eventloop_render(_mod.get(), &buf);
        } else {
            n = 0;
        }

        if (n > 0 && buf) {
            pa_simple_write(_audio.get(), buf, n * 2 * sizeof(int16_t), &error);
            need_drain = true;
        }

        if (need_drain && (n == 0 || !buf || _paused)) {
            pa_simple_drain(_audio.get(), &error);
            need_drain = false;
        }

        eventDispatcher()->processEvents((_paused || n == 0) ? QEventLoop::WaitForMoreEvents
                                                             : QEventLoop::AllEvents);
    }
}

void UpseModule::seek(int pos)
{
    if (!_mod) {
        return;
    }

    upse_eventloop_seek(_mod.get(), pos);
}

void UpseModule::load_file(QString const& file_name)
{
    _mod.reset(upse_module_open(file_name.toStdString().c_str(), &stdio_funcs));

    if (!_mod) {
        return;
    }

    _paused = false;
    emit total_time_changed(std::chrono::milliseconds{_mod->metadata->length});
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
    if (!_mod || _paused) {
        return;
    }
    emit seek_changed(std::chrono::milliseconds{upse_eventloop_tell_seek(_mod.get())});
}
