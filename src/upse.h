#pragma once

#include "audio.h"

#include "libupse/upse.h"

#include <QThread>
#include <QTimer>

#include <memory>
#include <chrono>
#include <vector>

struct upse_module_deleter {
    static void operator()(upse_module_t* mod) noexcept { upse_module_close(mod); }
};

using upse_module_ptr = std::unique_ptr<upse_module_t, upse_module_deleter>;

class UpseModule : public QThread {
    Q_OBJECT

public:
    explicit UpseModule(QObject *parent = nullptr);
    ~UpseModule() = default;

    void run() override;

    operator bool() const { return _mod.get(); }

signals:
    void total_time_changed(std::chrono::milliseconds ms);
    void seek_changed(std::chrono::milliseconds ms);

public slots:
    void seek(int pos);
    void toggle_pause();
    void pause(bool state);
    void shutdown();
    void load_file(QString const& file_name);

private slots:
    void slow_timer_fired();

private:
    upse_module_ptr _mod;
    pa_simple_unique_ptr _audio;
    bool _paused{};
    bool _shutdown{};
    QTimer _slow_timer;
    std::vector<std::pair<std::chrono::milliseconds, upse_snapshot_t>> _snapshots;
};
