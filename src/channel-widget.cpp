#include "channel-widget.h"

#include <QStyle>

// static constexpr char const* notes[] = {
//     "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
static constexpr char const* notes[] =
    {"Do", "Do#", "Re", "Re#", "Mi", "Fa", "Fa#", "Sol", "Sol#", "La", "La#", "Si"};

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
    auto const text_color = _ui.centsLabel->style()->standardPalette().text().color();
    _ui.centsLabel->setStyleSheet(QString::asprintf(
        "color: rgba(%d,%d,%d,0.5)", text_color.red(), text_color.green(), text_color.blue()));

    _ui.centsLabel->setFixedWidth(QFontMetrics(font).horizontalAdvance("+00"));
    _ui.freqLabel->setFixedWidth(QFontMetrics(font).horizontalAdvance(*std::ranges::max_element(
        notes, [](auto&& a, auto&& b) { return std::strlen(a) < std::strlen(b); })));

    _ui.soundMeterBar->setMultiplier(1.5);
    _ui.soundMeterBar->setOrientation(Qt::Orientation::Vertical);
    _ui.soundMeterBar->setLowpassDecay(0.5);
    _ui.soundMeterBar->setFixedHeight(65);
    _ui.volumeBar->setFixedHeight(65);

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
    auto const cents = [&] -> int {
        static constexpr auto C0 = 16.351597831;
        if (std::isnan(freq) || freq <= 0) {
            return -1;
        }
        auto const log2 = std::log2(freq / C0);
        return std::round(log2 * 1200);
    }();

    if (cents < 0) {
        if (_timer.isActive()) {
            return;
        }
        QObject::connect(&_timer, &QTimer::timeout, this, [this] {
            _ui.freqLabel->setText(QString{"-"});
            _ui.octaveLabel->setText(QString{""});
            _ui.centsLabel->setText(QString{""});
        });
        _timer.setInterval(500);
        _timer.setSingleShot(true);
        _timer.start();
        return;
    }

    _timer.stop();

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
