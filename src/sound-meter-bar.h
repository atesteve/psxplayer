#pragma once

#include <QWidget>

class SoundMeterBar : public QWidget {
    Q_OBJECT

    Q_PROPERTY(Qt::Orientation orientation READ orientation WRITE setOrientation)
    Q_PROPERTY(float lowpassDecay READ lowpassDecay WRITE setLowpassDecay)

public:
    explicit SoundMeterBar(QWidget* p = nullptr);

    Qt::Orientation orientation() const { return _orientation; }
    void setOrientation(Qt::Orientation orientation) { _orientation = orientation; }

    float lowpassDecay() const { return _decay; }
    void setLowpassDecay(float decay) { _decay = decay; }

public slots:
    void set_level(float l, float r);

private:
    void paintEvent(QPaintEvent*) override;
    void paint_horizontal(QPainter& painter, QColor bar_color);
    void paint_vertical(QPainter& painter, QColor bar_color);

    Qt::Orientation _orientation = Qt::Orientation::Horizontal;
    float _l{};
    float _r{};
    float _decay = 1;
};
