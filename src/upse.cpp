#include "upse.h"

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

UpseModule::UpseModule(std::string const& file_name)
    : _mod{upse_module_open(file_name.c_str(), &stdio_funcs)}
{
    if (!_mod) {
        return;
    }

    _audio = open_sound_device(_mod->metadata->rate);

    if (!_audio) {
        _mod.reset();
        return;
    }

    _thread.emplace([this] {
        int16_t* buf;
        int n, error;
        do {
            n = upse_eventloop_render(_mod.get(), &buf);
            if (n > 0) {
                pa_simple_write(_audio.get(), buf, n * 2 * sizeof(int16_t), &error);
            } else {
                pa_simple_drain(_audio.get(), &error);
            }
            if (_pause) {
                _continue->get();
            }
        } while (n > 0);
    });
}

void UpseModule::seek(float pos)
{
    if (!_mod) {
        return;
    }
    pos = std::min<float>(pos, 1);
    pos = std::max<float>(pos, 0);

    float const length = _mod->metadata->length;

    upse_eventloop_seek(_mod.get(), static_cast<uint32_t>(length * pos));
}

UpseModule::~UpseModule()
{
    if (_mod) {
        upse_eventloop_stop(_mod.get());
        play();
    }
}
