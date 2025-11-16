#pragma once

#include "channel-mapper/channel-mapper.h"
#include "audio.h"

#include "libupse/upse.h"

#include <QThread>
#include <QTimer>

#include <memory>
#include <chrono>
#include <vector>
#include <future>
#include <unordered_map>

struct upse_module_deleter {
    static void operator()(upse_module_t* mod) noexcept { upse_module_close(mod); }
};

using upse_module_ptr = std::unique_ptr<upse_module_t, upse_module_deleter>;

class UpseModule : public QThread {
    Q_OBJECT

public:
    enum class State {
        Unloaded,
        Paused,
        Playing,
        Stopped,
        Seeking,
    };

    explicit UpseModule(QObject* parent = nullptr);
    ~UpseModule() = default;

    void run() override;

    operator bool() const { return _mod.get(); }

signals:
    void total_time_changed(std::chrono::milliseconds ms);
    void seek_changed(std::chrono::milliseconds ms);
    void state_changed(State state);
    void sound_level_changed(float l, float r);
    void channel_sound_level_changed(int ch, float l, float r);
    void channel_frequency_changed(int ch, double freq);
    void channel_fired(int ch);
    void supported_channels(int n_channels);

public slots:
    void seek(int pos);
    void toggle_pause();
    void stop();
    void shutdown();
    void load_file(QString const& file_name);
    void set_speed(float speed);
    void mute_channel(int ch, bool muted);
    void set_channel_vol(int ch, float vol);

private slots:
    void slow_timer_fired();
    void fast_timer_fired();

private:
    struct ChannelState {
        bool muted{};
        float vol{1};
    };

    struct Freq {
        Freq(double value);
        Freq(std::future<double> future);

        std::optional<double> get();

        std::optional<double> _value;
        std::future<double> _future;
    };

    void update_mapped_channels();
    void take_snapshot();
    void set_state(State new_state);
    void handle_channel_fire();
    void find_sample_frequency();

    static void jal_hook(void* self, upse_module_instance_t* ins);
    void jal_hook(upse_module_instance_t* ins);
    static void sw_hook(void* self,
                        upse_module_instance_t* ins,
                        mem_access_size_t size,
                        uint32_t addr,
                        uint32_t data);
    void sw_hook(upse_module_instance_t* ins, mem_access_size_t size, uint32_t addr, uint32_t data);

    upse_module_ptr _mod;
    std::optional<Audio> _audio;

    State _state{};
    bool _paused{};
    bool _shutdown{};
    float _speed{1};
    int _stopped_cycles{};
    QTimer _slow_timer;
    QTimer _fast_timer;
    std::vector<ChannelState> _channel_state;

    std::unique_ptr<ChannelMapper> _channel_mapper;

    std::vector<std::pair<std::chrono::milliseconds, std::pair<upse_snapshot_t, std::any>>>
        _snapshots;

    emulation_control_t _control{
        .input =
            {
                .speed_multiplier = 1,
                .channel = {},
            },
        .output = {},
        .hooks =
            {
                .data = this,
                .jal = UpseModule::jal_hook,
                .sw = sw_hook,
            },
    };

    struct pair_hash {
        static size_t operator()(std::pair<uint32_t, uint32_t> const& p) noexcept
        {
            return std::hash<uint32_t>{}(p.first) ^ std::hash<uint32_t>{}(p.second);
        }
    };

    std::unordered_map<std::pair<uint32_t, uint32_t>, Freq, pair_hash> _sample_freq;
    std::mutex _sample_freq_mutex;
};
