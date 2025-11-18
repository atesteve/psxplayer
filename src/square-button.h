#pragma once

#include <QPushButton>

class SquareButton : public QPushButton {
    Q_OBJECT
public:
    using QPushButton::QPushButton;

    QSize sizeHint() const override
    {
        auto hint = QPushButton::sizeHint();
        hint.setWidth(hint.height());
        return hint;
    }

    QSize minimumSizeHint() const override
    {
        auto hint = QPushButton::minimumSizeHint();
        hint.setWidth(hint.height());
        return hint;
    }
};
