#include "channel-widget.h"

#include <QStyle>

ChannelWidget::ChannelWidget(QWidget* parent)
    : QWidget{parent}
{
    ui.setupUi(this);
    ui.muteButton->setIcon(style()->standardIcon(QStyle::SP_MediaVolumeMuted));
    ui.muteButton->setFixedWidth(ui.muteButton->height());

    ui.soloButton->setText("S");
    ui.soloButton->setFixedWidth(ui.soloButton->height());
}
