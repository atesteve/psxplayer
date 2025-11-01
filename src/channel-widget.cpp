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

    auto font = ui.freqLabel->font();
    font.setPointSizeF(font.pointSizeF() * 0.85);
    ui.freqLabel->setFont(font);
    ui.octaveLabel->setFont(font);
    ui.centsLabel->setFont(font);

    ui.centsLabel->setFixedWidth(QFontMetrics(font).boundingRect("+50").width());
    ui.octaveLabel->setFixedWidth(QFontMetrics(font).boundingRect('0').width());
}
