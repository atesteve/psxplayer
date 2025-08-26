#pragma once

#include "ui_channel-widget.h"

#include <QWidget>

class ChannelWidget : public QWidget {
    Q_OBJECT
public:
    explicit ChannelWidget(QWidget* parent = nullptr);
    Ui::ChannelWidget ui;
};
