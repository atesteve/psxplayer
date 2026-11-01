#include "psx-player.h"

#include "psf/psf.h"

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

    return ret;
}

int64_t PsxPlayer::render_audio(std::span<int16_t> output, size_t n_samples)
{
    if (_emu->tell_seek() < _pos) {
        _emu->fast_forward(_pos, 100ms);
        return 0;
    }

    auto const ret = _emu->render_audio(output, n_samples);
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
