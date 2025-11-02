#include "channel-widget.h"

#include <QStyle>

ChannelWidget::ChannelWidget(QWidget* parent)
    : QWidget{parent}
{
    _ui.setupUi(this);
    _ui.muteButton->setIcon(style()->standardIcon(QStyle::SP_MediaVolumeMuted));
    _ui.muteButton->setFixedWidth(_ui.muteButton->height());

    _ui.soloButton->setText("S");
    _ui.soloButton->setFixedWidth(_ui.soloButton->height());

    auto font = _ui.freqLabel->font();
    font.setPointSizeF(font.pointSizeF() * 0.85);
    _ui.freqLabel->setFont(font);
    _ui.octaveLabel->setFont(font);
    _ui.centsLabel->setFont(font);

    _ui.centsLabel->setFixedWidth(QFontMetrics(font).boundingRect("+00").width());
    _ui.octaveLabel->setFixedWidth(QFontMetrics(font).boundingRect('0').width());

    _ui.soundMeterBar->setOrientation(Qt::Orientation::Vertical);
    _ui.soundMeterBar->setLowpassDecay(0.5);

    QObject::connect(_ui.muteButton, &QPushButton::toggled, this, [this](bool checked) {
        emit muteChanged(checked);
    });

    QObject::connect(_ui.soloButton, &QPushButton::toggled, this, [this](bool checked) {
        emit soloChanged(checked);
    });

    QObject::connect(_ui.volumeBar, &QSlider::valueChanged, this, [this](int value) {
        emit volumeChanged(value / 100.f);
    });
}

void ChannelWidget::setTitle(QString const& title)
{
    _ui.title->setText(title);
}

void ChannelWidget::setSolo(bool solo)
{
    _ui.soloButton->setChecked(solo);
}

void ChannelWidget::setMute(bool mute)
{
    _ui.muteButton->setChecked(mute);
}

void ChannelWidget::setDisabled(bool disabled)
{
    _ui.volumeBar->setDisabled(disabled);
    _ui.title->setDisabled(disabled);
    _ui.freqLabel->setDisabled(disabled);
    _ui.octaveLabel->setDisabled(disabled);
    _ui.centsLabel->setDisabled(disabled);
    _ui.soundMeterBar->setDisabled(disabled);
}

void ChannelWidget::setSoundLevel(float l, float r)
{
    _ui.soundMeterBar->set_level(l, r);
}

void ChannelWidget::setFrequency(double freq)
{
    // static constexpr char const* notes[] = {
    //     "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    static constexpr char const* notes[] = {
        "Do", "Do#", "Re", "Re#", "Mi", "Fa", "Fa#", "Sol", "Sol#", "La", "La#", "Si"};

    auto const cents = [&] -> int {
        static constexpr auto C0 = 16.351597831;
        if (std::isnan(freq) || freq <= 0) {
            return -1;
        }
        auto const log2 = std::log2(freq / C0);
        return std::round(log2 * 1200);
    }();

    if (cents < 0) {
        _ui.freqLabel->setText(QString{"-"});
        _ui.octaveLabel->setText(QString{""});
        _ui.centsLabel->setText(QString{""});
        return;
    }

    auto octave = cents / 1200;
    auto octave_cents = cents % 1200;
    auto note = octave_cents / 100;
    auto note_cents = octave_cents % 100;

    if (note_cents >= 50) {
        note_cents -= 100;
        note += 1;
        if (note == 12) {
            note = 0;
            octave += 1;
        }
    }

    _ui.freqLabel->setText(QString{notes[note]});
    _ui.octaveLabel->setText(QString::number(octave));
    _ui.centsLabel->setText(QString::asprintf("%+d", note_cents));
}

void ChannelWidget::channelFired()
{
    _ui.soundMeterBar->channel_fired();
}
