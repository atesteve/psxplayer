#pragma once

#include <QPushButton>

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
};
