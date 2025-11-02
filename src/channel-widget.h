#pragma once

#include "ui_channel-widget.h"

#include <QWidget>

class ChannelWidget : public QWidget {
    Q_OBJECT
public:
    explicit ChannelWidget(QWidget* parent = nullptr);

    void setTitle(QString const& title);
    void setDisabled(bool disabled);

public slots:
    void setSolo(bool solo);
    void setMute(bool mute);

    void setSoundLevel(float l, float r);

    void setFrequency(double freq);

    void channelFired();

signals:
    void muteChanged(bool);
    void soloChanged(bool);
    void volumeChanged(float value);

private:
    Ui::ChannelWidget _ui;
};
