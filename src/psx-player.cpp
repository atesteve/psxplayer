#include "psx-player.h"

#include "psf/psf.h"
#include "util/util.h"

using namespace std::literals;

std::expected<PsxPlayer, std::string> PsxPlayer::build(std::filesystem::path path)
{
    auto psf = load_psf(path);
    if (!psf) {
        return std::unexpected{std::move(psf.error())};
    }

    PsxPlayer ret;
    ret._emu = R3000::build();
    ret._emu->init(*psf);
    ret._duration = psf->get_length();
    ret._fadeout_length = psf->get_fadeout_length();
    ret._tags = std::move(psf->tags);
    ret._control = ret._emu->get_control();

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
    return _emu->take_snapshot();
}

void PsxPlayer::restore_snapshot(SnapshotBuffer const& snapshot)
{
    _emu->restore_snapshot(snapshot);
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
    return 24;
}

void PsxPlayer::mute_channel(size_t n, bool muted)
{
    _control->channel[n].muted = muted;
}

auto PsxPlayer::get_channel_information() const -> std::vector<ChannelInformation>
{
    std::vector<ChannelInformation> ret;
    ret.reserve(std::size(_control->channel));
    for (auto& ch : _control->channel) {
        ret.push_back({
            .samples = {ch.window_l, ch.window_r},
            .playing = ch.playing,
            .fired = ch.fire,
        });
        ch.fire = false;
    }
    return ret;
}
