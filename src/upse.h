#pragma once

#include "audio.h"

#include "libupse/upse.h"

#include <memory>
#include <thread>
#include <string>
#include <optional>

struct upse_module_deleter {
    void operator()(upse_module_t* mod) const noexcept { upse_module_close(mod); }
};

using upse_unique_ptr = std::unique_ptr<upse_module_t, upse_module_deleter>;

class UpseModule {
public:
    explicit UpseModule(std::string const& file_name);
    ~UpseModule();

    operator bool() const { return _mod.get(); }

private:
    upse_unique_ptr _mod;
    pa_simple_unique_ptr _audio;
    std::optional<std::jthread> _thread;
};
