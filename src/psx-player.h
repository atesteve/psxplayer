// SPDX-FileCopyrightText: 2026 Aitor Esteve Alvarado
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "psxemu/core/r3000.h"

#include <memory>
#include <expected>
#include <filesystem>
#include <chrono>
#include <future>
#include <mutex>

class ChannelMapper;

class PsxPlayer {
public:
    static std::expected<std::unique_ptr<PsxPlayer>, std::string> build(std::filesystem::path path);
    ~PsxPlayer();

    int64_t render_audio(std::span<int16_t> output, size_t n_samples);
    SnapshotBuffer take_snapshot();
    void restore_snapshot(SnapshotBuffer const& snapshot);

    auto get_duration() const { return _duration; }

    std::chrono::milliseconds tell_seek() const;
    void seek(std::chrono::milliseconds pos);
    bool seek_done() const;

    void set_playback_speed(float speed);

    using window_t = std::pair<std::span<int16_t const>, std::span<int16_t const>>;

    window_t get_master_window() const;

    size_t n_channels() const;
    void mute_channel(size_t n, bool muted);

    struct ChannelInformation {
        window_t samples{};
        double freq{};
        bool playing{};
        bool fired{};
    };

    std::vector<ChannelInformation> get_channel_information();

private:
    explicit PsxPlayer() = default;

    void update_mapped_channels();
    double get_sample_frequency(R3000::Sample const* s, uint64_t key);
    double get_channel_frequency(int c);

    struct ChannelControl {
        bool muted{};
        float volume{1};
    };

    std::unique_ptr<R3000> _emu;
    std::chrono::milliseconds _duration;
    std::chrono::milliseconds _fadeout_length;
    std::chrono::milliseconds _pos{};
    std::unordered_map<std::string, std::string> _tags;
    R3000::Control* _control{};
    std::unique_ptr<ChannelMapper> _mapper{};
    std::vector<ChannelControl> _channel_control{};

    struct Freq {
        std::optional<double> _value;
        std::future<double> _future;
    };

    std::unordered_map<uint64_t, Freq> _sample_freq;
    std::mutex _sample_freq_mutex;
};
