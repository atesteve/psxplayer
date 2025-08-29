#include "sound-meter-bar.h"

#include <fmt/format.h>

#include <QPainter>
#include <QPalette>
#include <QPainterStateGuard>

#include <cmath>
#include <algorithm>

namespace {

constexpr auto MARGIN = 4;
constexpr auto BAR_WIDTH = 0.25f;

float toDB(float rms)
{
    if (rms <= 0) {
        return 0;
    }
    float ret = 20 * std::log10(rms);
    // Normalize -95,0 to 0,1.
    return (std::clamp<float>(ret, -95, 0) + 95) / 95;
}

} // namespace

SoundMeterBar::SoundMeterBar(QWidget* p)
    : QWidget{p}
{}

void SoundMeterBar::set_level(float l, float r)
{
    _l = toDB(l);
    _r = toDB(r);
    update();
}

void SoundMeterBar::paintEvent(QPaintEvent*)
{
    auto const& palette = this->palette();
    QPainter painter(this);

    auto const rect = this->rect();
    painter.fillRect(rect, palette.alternateBase());

    auto font = painter.font();
    auto const fontHeight = QFontMetricsF{font}.ascent();

    auto const desiredFontHeight = rect.height() * 0.4;
    font.setPointSizeF(font.pointSizeF() * (desiredFontHeight / fontHeight));
    painter.setFont(font);
    auto textColor = palette.text().color();
    textColor.setAlphaF(.5);

    painter.setPen(textColor);
    painter.drawText(rect.topLeft() + QPointF{MARGIN, rect.height() * 0.3 + QFontMetricsF{font}.ascent() / 2},
                     "L");
    painter.drawText(rect.topLeft() + QPointF{MARGIN, rect.height() * 0.7 + QFontMetricsF{font}.ascent() / 2},
                     "R");

    auto const draw_bar = [&](float value, QPointF base, int max_length) {
        QPainterStateGuard guard{&painter};
        painter.setPen(Qt::NoPen);
        painter.fillRect(QRectF{base.x(),
                                base.y() - rect.height() * (BAR_WIDTH / 2),
                                std::lerp(0, max_length, value),
                                rect.height() * BAR_WIDTH},
                         palette.highlight());
    };

    auto const text_width = QFontMetricsF{font}.horizontalAdvance("L");
    draw_bar(_l,
             rect.topLeft() + QPointF{MARGIN * 2 + text_width, rect.height() * 0.3},
             rect.width() - MARGIN * 3 - text_width);
    draw_bar(_r,
             rect.topLeft() + QPointF{MARGIN * 2 + text_width, rect.height() * 0.7},
             rect.width() - MARGIN * 3 - text_width);
}
