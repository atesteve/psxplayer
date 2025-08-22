#include "upse.h"

#include <QAbstractEventDispatcher>

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

UpseModule::UpseModule(std::string const& file_name, QObject* parent)
    : QThread{parent}
    , _mod{upse_module_open(file_name.c_str(), &stdio_funcs)}
{
    if (!_mod) {
        return;
    }

    _audio = open_sound_device(_mod->metadata->rate);

    if (!_audio) {
        _mod.reset();
        return;
    }
}

void UpseModule::run()
{
    if (!_mod) {
        return;
    }

    int16_t* buf;
    int n, error;

    while (!_shutdown) {
        n = upse_eventloop_render(_mod.get(), &buf);

        if (n > 0) {
            pa_simple_write(_audio.get(), buf, n * 2 * sizeof(int16_t), &error);
        } else {
            pa_simple_drain(_audio.get(), &error);
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

    float pos_f = pos / 100.0f;

    pos_f = std::min<float>(pos_f, 1);
    pos_f = std::max<float>(pos_f, 0);

    float const length = _mod->metadata->length;

    upse_eventloop_seek(_mod.get(), static_cast<uint32_t>(length * pos_f));
}

void UpseModule::toggle_pause() { _paused = !_paused; }

void UpseModule::shutdown() { _shutdown = true; }
