#include "channel-widget.h"

#include <QStyle>

ChannelWidget::ChannelWidget(QWidget* parent)
    : QWidget{parent}
{
    ui.setupUi(this);
    ui.muteButton->setIcon(style()->standardIcon(QStyle::SP_MediaVolumeMuted));
    ui.soloButton->setIcon(style()->standardIcon(QStyle::SP_MediaVolume));
}
