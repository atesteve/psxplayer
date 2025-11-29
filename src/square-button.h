#pragma once

#include <QPushButton>
#include <QStylePainter>
#include <QStyleOptionButton>
#include <QPaintEvent>

class SquareButton : public QPushButton {
    Q_OBJECT
public:
    using QPushButton::QPushButton;

    QSize sizeHint() const override
    {
        auto hint = QPushButton::sizeHint();
        auto const dim = std::max(hint.height(), hint.width());
        hint.setWidth(dim);
        hint.setHeight(dim);
        return hint;
    }

    QSize minimumSizeHint() const override
    {
        auto hint = QPushButton::minimumSizeHint();
        auto const dim = std::max(hint.height(), hint.width());
        hint.setWidth(dim);
        hint.setHeight(dim);
        return hint;
    }

    void setTextOffset(int x, int y)
    {
        _textOffsetX = x;
        _textOffsetY = y;
    }

    void paintEvent(QPaintEvent* e) override
    {
        QStylePainter p{this};
        QStyleOptionButton option;
        initStyleOption(&option);
        option.text = "";
        p.drawControl(QStyle::CE_PushButton, option);

        QFontMetrics metrics{font()};
        QRect rect{0, 0, metrics.horizontalAdvance(text()), metrics.xHeight()};
        rect.moveCenter(e->rect().center() - QPoint{0, 2});

        p.drawItemText(e->rect().adjusted(_textOffsetX, _textOffsetY, _textOffsetX, _textOffsetY),
                       Qt::AlignCenter,
                       palette(),
                       isEnabled(),
                       text());
    }

private:
    int _textOffsetX{};
    int _textOffsetY{};
};
