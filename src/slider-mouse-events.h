#pragma once

#include <QSlider>

class QSliderMouseEvent : public QSlider {
    Q_OBJECT

public:
    explicit QSliderMouseEvent(Qt::Orientation orientation, QWidget* parent = nullptr);
    explicit QSliderMouseEvent(QWidget* parent = nullptr);

signals:
    void mouseMoved(int value);

private:
    void mouseMoveEvent(QMouseEvent* event) override;
};
