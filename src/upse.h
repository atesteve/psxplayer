#pragma once

#include "audio.h"

#include "libupse/upse.h"

#include <memory>
#include <thread>
#include <string>
#include <optional>
#include <future>
#include <atomic>

struct upse_module_deleter {
    static void operator()(upse_module_t* mod) noexcept { upse_module_close(mod); }
};

using upse_module_ptr = std::unique_ptr<upse_module_t, upse_module_deleter>;

class UpseModule {
public:
    explicit UpseModule(std::string const& file_name);
    ~UpseModule();

    void pause()
    {
        if (_pause) {
            return;
        }
        _continue_promise.emplace();
        _continue.emplace(_continue_promise->get_future());
        _pause = true;
    }

    void play()
    {
        if (!_pause) {
            return;
        }
        _pause = false;
        _continue_promise->set_value();
    }

    void seek(float pos);

    operator bool() const { return _mod.get(); }

private:
    std::optional<std::promise<void>> _continue_promise;
    std::optional<std::future<void>> _continue;
    std::atomic_bool _pause;
    upse_module_ptr _mod;
    pa_simple_unique_ptr _audio;
    std::optional<std::jthread> _thread;
};
