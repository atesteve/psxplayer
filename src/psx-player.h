// SPDX-FileCopyrightText: 2026 Aitor Esteve Alvarado
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "psxemu/core/r3000.h"

#include <memory>
#include <expected>
#include <filesystem>
#include <chrono>

class PsxPlayer {
public:
    static std::expected<PsxPlayer, std::string> build(std::filesystem::path path);

    int64_t render_audio(std::span<int16_t> output, size_t n_samples);
    SnapshotBuffer take_snapshot();
    void restore_snapshot(SnapshotBuffer const& snapshot);

    auto get_duration() const {
        return _duration;
    }

    std::chrono::milliseconds tell_seek() const;
    void seek(std::chrono::milliseconds pos);
    bool seek_done() const;

private:
    explicit PsxPlayer() = default;

    std::unique_ptr<R3000> _emu;
    std::chrono::milliseconds _duration;
    std::chrono::milliseconds _fadeout_length;
    std::chrono::milliseconds _pos{};
};
