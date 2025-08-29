#pragma once

#include <QWidget>

class SoundMeterBar : public QWidget {
    Q_OBJECT

    Q_PROPERTY(Qt::Orientation orientation READ orientation WRITE setOrientation)

public:
    explicit SoundMeterBar(QWidget* p = nullptr);

    Qt::Orientation orientation() const { return _orientation; }
    void setOrientation(Qt::Orientation orientation) { _orientation = orientation; }

public slots:
    void set_level(float l, float r);

private:
    void paintEvent(QPaintEvent*) override;
    void paint_horizontal(QPainter& painter);
    void paint_vertical(QPainter& painter);

    Qt::Orientation _orientation = Qt::Orientation::Horizontal;
    float _l{};
    float _r{};
};
