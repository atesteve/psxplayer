#include "slider-mouse-events.h"

#include <QMouseEvent>
#include <QStyleOptionSlider>

#include <fmt/format.h>

QSliderMouseEvent::QSliderMouseEvent(Qt::Orientation orientation, QWidget* parent)
    : QSlider{orientation, parent}
{
    setMouseTracking(true);
}

QSliderMouseEvent::QSliderMouseEvent(QWidget* parent)
    : QSlider{parent}
{
    setMouseTracking(true);
}

void QSliderMouseEvent::mouseMoveEvent(QMouseEvent* event)
{
    QSlider::mouseMoveEvent(event);

    QStyleOptionSlider opt;
    initStyleOption(&opt);
    QRect gr = style()->subControlRect(QStyle::CC_Slider, &opt, QStyle::SC_SliderGroove, this);
    QRect sr = style()->subControlRect(QStyle::CC_Slider, &opt, QStyle::SC_SliderHandle, this);

    int sliderLength = sr.width();
    int sliderMin = gr.x() + sliderLength / 2;
    int sliderMax = gr.right() - sliderLength / 2 - 1;

    int const val = QStyle::sliderValueFromPosition(minimum(),
                                                    maximum(),
                                                    event->position().x() - sliderMin,
                                                    sliderMax - sliderMin,
                                                    opt.upsideDown);

    emit mouseMoved(val);
}
