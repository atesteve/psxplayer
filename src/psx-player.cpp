#include "psx-player.h"

#include "psf/psf.h"

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

size_t PsxPlayer::render_audio(std::span<int16_t> output, size_t n_samples)
{
    return _emu->render_audio(output, n_samples);
}

SnapshotBuffer PsxPlayer::take_snapshot()
{
    return _emu->take_snapshot();
}

std::chrono::milliseconds PsxPlayer::tell_seek() const
{
    return _emu->tell_seek();
}
