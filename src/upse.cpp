#include "upse.h"

namespace {

upse_iofuncs_t stdio_funcs{
    .open_impl = (void* (*)(const char* path, const char* mode))fopen,
    .read_impl = (size_t (*)(void* ptr, size_t size, size_t nmemb, void* file))fread,
    .seek_impl = (int (*)(void* file, long offset, int whence))fseek,
    .close_impl = (int (*)(void* file))fclose,
    .tell_impl = (long (*)(void* file))ftell,
};

}

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

    upse_eventloop_set_audio_callback(
        _mod.get(),
        [](unsigned char* data, long length, const void* userdata) {
            auto* const pa = (pa_simple*)userdata;
            int error;
            pa_simple_write(pa, data, length, &error);
        },
        _audio.get());

    _thread.emplace([this] { upse_eventloop_run(_mod.get()); });
}

UpseModule::~UpseModule()
{
    if (_mod) {
        upse_eventloop_stop(_mod.get());
    }
}
