#include "psx-player.h"

#include "psf/psf.h"
#include "util/util.h"
#include "channel-mapper/channel-mapper.h"
#include "psxemu/spu/adpcm.h"

#include <fmt/format.h>

#include <optional>

using namespace std::literals;

namespace {

std::optional<std::string_view> get_tag(auto const& tags, std::string_view key)
{
    auto const it = tags.find(std::string{key});
    if (it == tags.cend()) {
        return std::nullopt;
    } else {
        return {it->second};
    }
}

} // namespace

PsxPlayer::~PsxPlayer() = default;

std::expected<std::unique_ptr<PsxPlayer>, std::string> PsxPlayer::build(std::filesystem::path path)
{
    auto psf = load_psf(path);
    if (!psf) {
        return std::unexpected{std::move(psf.error())};
    }

    std::unique_ptr<PsxPlayer> ret{new PsxPlayer};
    ret->_emu = R3000::build();
    ret->_emu->init(*psf);
    ret->_duration = psf->get_length();
    ret->_fadeout_length = psf->get_fadeout_length();
    ret->_tags = std::move(psf->tags);
    ret->_control = ret->_emu->get_control();
    ret->_mapper = ChannelMapper::build(ret->_emu.get(),
                                        get_tag(ret->_tags, "game").value_or(""),
                                        get_tag(ret->_tags, "title").value_or(""));

    ret->_channel_control.resize(ret->_mapper->supported_channels());

    ret->_emu->set_jal_hook([self = ret.get()](R3000* emu) {
        if (self->_mapper->jal_hook(emu)) {
            self->update_mapped_channels();
        }
    });
    ret->_emu->set_sw_hook(
        [self = ret.get()](R3000* emu, uint32_t size, uint32_t addr, uint32_t data) {
            if (self->_mapper->sw_hook(emu, size, addr, data)) {
                self->update_mapped_channels();
            }
        });

    return ret;
}

int64_t PsxPlayer::render_audio(std::span<int16_t> output, size_t n_samples)
{
    if (_emu->tell_seek() < _pos) {
        ScopeGuard restore_playback_speed{[prev_speed = _control->playback_speed, this] {
            _control->playback_speed = prev_speed;
        }};
        _control->playback_speed = 10240;
        _emu->fast_forward(_pos, 100ms);
        return 0;
    }

    auto const ret = _emu->render_audio(output, n_samples, 50ms);
    _pos = _emu->tell_seek();
    return ret;
}

SnapshotBuffer PsxPlayer::take_snapshot()
{
    SnapshotBuffer snapshot;
    SnapshotSerializer serializer{&snapshot};

    _emu->take_snapshot(serializer);
    _mapper->take_snapshot(serializer);

    return snapshot.compress();
}

void PsxPlayer::restore_snapshot(SnapshotBuffer const& snapshot)
{
    SnapshotBuffer const decompressed = snapshot.decompress();
    SnapshotDeserializer deserializer{&decompressed};

    _emu->restore_snapshot(deserializer);
    _mapper->restore_snapshot(deserializer);
    _emu->flush_reverb();

    update_mapped_channels();
}

std::chrono::milliseconds PsxPlayer::tell_seek() const
{
    return _emu->tell_seek();
}

void PsxPlayer::seek(std::chrono::milliseconds pos)
{
    _pos = pos;
}

bool PsxPlayer::seek_done() const
{
    return _pos <= _emu->tell_seek();
}

void PsxPlayer::set_playback_speed(float speed)
{
    _control->playback_speed = 256 * speed;
}

std::pair<std::span<int16_t const>, std::span<int16_t const>> PsxPlayer::get_master_window() const
{
    return {_control->window_l, _control->window_r};
}

size_t PsxPlayer::n_channels() const
{
    return _mapper ? _mapper->supported_channels() : 0;
}

void PsxPlayer::mute_channel(size_t n, bool muted)
{
    _channel_control[n].muted = muted;
    update_mapped_channels();
}

auto PsxPlayer::get_channel_information() -> std::vector<ChannelInformation>
{
    if (!_mapper) {
        return {};
    }
    std::vector<ChannelInformation> ret;
    ret.resize(_mapper->supported_channels());
    for (auto&& [i, ch] : std::views::enumerate(_control->channel)) {
        auto const logical_channel = _mapper->physical_to_logical(i);
        if (logical_channel < 0) {
            continue;
        }
        ret[logical_channel] = {
            .samples = {ch.window_l, ch.window_r},
            .freq = get_channel_frequency(i),
            .playing = ch.playing,
            .fired = ch.fire,
        };
        ch.fire = false;
    }
    return ret;
}

void PsxPlayer::update_mapped_channels()
{
    for (auto&& [i, ch] : std::views::enumerate(_control->channel)) {
        auto const logical_channel = _mapper->physical_to_logical(i);
        if (logical_channel == -1) {
            continue;
        }
        ch.muted = _channel_control[logical_channel].muted;
    }
}

double PsxPlayer::get_channel_frequency(int c)
{
    auto const& channel = _control->channel[c];
    if (!channel.playing) {
        return 0;
    }
    auto const sample_freq = get_sample_frequency(channel.sample, channel.sample_key);
    auto const channel_multiplier = channel.adpcm_freq / 4096.0;
    return sample_freq * channel_multiplier;
}

double PsxPlayer::get_sample_frequency(R3000::Sample const* sample, uint64_t key)
{
    if (!sample) {
        return 0;
    }
    auto& freq = _sample_freq[key];
    if (freq._value) {
        return *freq._value;
    }
    if (!freq._future.valid()) {
        freq._future = std::async(std::launch::async, [this, sample, key] -> double {
            return find_sample_freq(
                sample,
                _sample_freq_mutex,
                fmt::format("{:#x}, {:#x}", uint32_t(key << 1), key >> 31));
        });
    }
    auto const future_state = freq._future.wait_for(0s);
    if (future_state != std::future_status::ready) {
        return 0;
    }
    freq._value = freq._future.get();
    return *freq._value;
}
