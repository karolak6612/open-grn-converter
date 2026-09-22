#pragma once

#include <QWidget>
#include <QPainter>
#include <QPen>
#include <QColor>

namespace grn {

class SectionCard : public QWidget {
public:
    explicit SectionCard(QWidget* parent = nullptr) : QWidget(parent) {
        setAttribute(Qt::WA_StyledBackground, false);
    }

protected:
    void paintEvent(QPaintEvent* /*event*/) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setBrush(QColor(0xeb, 0xeb, 0xeb));
        p.setPen(QPen(QColor(0xde, 0xde, 0xde), 1));
        p.drawRoundedRect(rect().adjusted(0, 0, -1, -1), 6.0, 6.0);
    }
};

} // namespace grn
