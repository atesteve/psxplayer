#pragma once

#include "audio.h"

#include "libupse/upse.h"

#include <QThread>

#include <memory>
#include <string>

struct upse_module_deleter {
    static void operator()(upse_module_t* mod) noexcept { upse_module_close(mod); }
};

using upse_module_ptr = std::unique_ptr<upse_module_t, upse_module_deleter>;

class UpseModule : public QThread {
    Q_OBJECT

public:
    explicit UpseModule(std::string const& file_name, QObject *parent = nullptr);
    ~UpseModule() = default;

    void run() override;

    operator bool() const { return _mod.get(); }

public slots:
    void seek(int pos);
    void toggle_pause();
    void shutdown();

private:
    upse_module_ptr _mod;
    pa_simple_unique_ptr _audio;
    bool _paused{};
    bool _shutdown{};
};
