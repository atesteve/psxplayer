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
    auto new_l = toDB(l);
    auto new_r = toDB(r);
    _l += (new_l - _l) * _decay;
    _r += (new_r - _r) * _decay;
    update();
}

void SoundMeterBar::paintEvent(QPaintEvent*)
{
    auto const& palette = this->palette();
    QPainter painter{this};

    auto const rect = this->rect();
    painter.fillRect(rect, palette.alternateBase());
    auto bar_color = palette.highlight().color();
    if (palette.currentColorGroup() == QPalette::ColorGroup::Disabled) {
        bar_color.setAlphaF(.5);
    }

    if (_orientation == Qt::Orientation::Horizontal) {
        paint_horizontal(painter, bar_color);
    } else {
        paint_vertical(painter, bar_color);
    }
}

void SoundMeterBar::paint_horizontal(QPainter& painter, QColor bar_color)
{
    auto const rect = this->rect();
    auto const& palette = this->palette();

    auto font = painter.font();
    auto const fontHeight = QFontMetricsF{font}.ascent();

    auto const desiredFontHeight = rect.height() * 0.4;
    font.setPointSizeF(font.pointSizeF() * (desiredFontHeight / fontHeight));
    painter.setFont(font);
    auto textColor = palette.text().color();
    textColor.setAlphaF(.5);

    painter.setPen(textColor);
    painter.drawText(rect.topLeft() + QPointF{MARGIN, rect.height() * 0.3 + desiredFontHeight / 2},
                     "L");
    painter.drawText(rect.topLeft() + QPointF{MARGIN, rect.height() * 0.7 + desiredFontHeight / 2},
                     "R");

    auto const draw_bar = [&](float value, QPointF base, int max_length) {
        QPainterStateGuard guard{&painter};
        painter.setPen(Qt::NoPen);
        painter.fillRect(QRectF{base.x(),
                                base.y() - rect.height() * (BAR_WIDTH / 2),
                                std::lerp(0, max_length, value),
                                rect.height() * BAR_WIDTH},
                         bar_color);
    };

    auto const text_width = QFontMetricsF{font}.horizontalAdvance("L");
    draw_bar(_l,
             rect.topLeft() + QPointF{MARGIN * 2 + text_width, rect.height() * 0.3},
             rect.width() - MARGIN * 3 - text_width);
    draw_bar(_r,
             rect.topLeft() + QPointF{MARGIN * 2 + text_width, rect.height() * 0.7},
             rect.width() - MARGIN * 3 - text_width);
}

void SoundMeterBar::paint_vertical(QPainter& painter, QColor bar_color)
{
    auto const rect = this->rect();
    auto const& palette = this->palette();

    auto font = painter.font();
    auto const fontWidth = QFontMetricsF{font}.horizontalAdvance("L");

    auto const desiredFontWidth = rect.width() * 0.15;
    font.setPointSizeF(font.pointSizeF() * (desiredFontWidth / fontWidth));
    painter.setFont(font);
    auto textColor = palette.text().color();
    textColor.setAlphaF(.5);

    painter.setPen(textColor);
    painter.drawText(
        rect.bottomLeft() + QPointF{rect.width() * 0.3 - desiredFontWidth / 2, -MARGIN}, "L");
    painter.drawText(
        rect.bottomLeft() + QPointF{rect.width() * 0.7 - desiredFontWidth / 2, -MARGIN}, "R");

    auto const draw_bar = [&](float value, QPointF base, int max_length) {
        QPainterStateGuard guard{&painter};
        painter.setPen(Qt::NoPen);
        painter.fillRect(QRectF{base.x() - rect.width() * (BAR_WIDTH / 2),
                                base.y(),
                                rect.width() * BAR_WIDTH,
                                -std::lerp(0, max_length, value)},
                         bar_color);
    };

    auto const text_height = QFontMetricsF{font}.ascent();
    draw_bar(_l,
             rect.bottomLeft() + QPointF{rect.width() * 0.3, -MARGIN * 2 - text_height},
             rect.height() - MARGIN * 3 - text_height);
    draw_bar(_r,
             rect.bottomLeft() + QPointF{rect.width() * 0.7, -MARGIN * 2 - text_height},
             rect.height() - MARGIN * 3 - text_height);
}
