#include "ui/tool_flyout.h"

#include <QAction>
#include <QFrame>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QVBoxLayout>

#include "constants/design_tokens.h"

namespace plnr::ui {

namespace {

constexpr int kLongPressMs = 300;
constexpr int kCornerPx = 10;        // clickable bottom-right corner region
constexpr int kTriangleLegPx = 6;    // painted marker, inset from the corner
constexpr int kTriangleInsetPx = 3;
constexpr int kPaletteIconPx = 24;

}  // namespace

ToolFlyoutButton::ToolFlyoutButton(const QList<QAction*>& variants, QWidget* parent)
    : QToolButton(parent), variants_(variants) {
    Q_ASSERT(!variants_.isEmpty());
    setToolButtonStyle(Qt::ToolButtonIconOnly);

    longPress_.setSingleShot(true);
    longPress_.setInterval(kLongPressMs);
    connect(&longPress_, &QTimer::timeout, this, [this]() {
        if (isDown()) {
            pressConsumed_ = true;
            openPalette();
        }
    });

    QAction* initial = variants_.first();
    for (QAction* variant : variants_) {
        if (variant->isCheckable() && variant->isChecked()) {
            initial = variant;
        }
        connect(variant, &QAction::toggled, this, [this, variant](bool on) {
            if (on) setFace(variant);
        });
    }
    setFace(initial);
}

void ToolFlyoutButton::setFace(QAction* action) {
    if (action == face_ || (face_ != nullptr && !action->isCheckable())) {
        return;
    }
    face_ = action;
    setDefaultAction(action);
}

bool ToolFlyoutButton::inCorner(const QPoint& pos) const {
    return pos.x() >= width() - kCornerPx && pos.y() >= height() - kCornerPx;
}

void ToolFlyoutButton::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        pressConsumed_ = false;
        if (inCorner(event->position().toPoint())) {
            pressConsumed_ = true;
            openPalette();
            return;
        }
        longPress_.start();
    }
    QToolButton::mousePressEvent(event);
}

void ToolFlyoutButton::mouseReleaseEvent(QMouseEvent* event) {
    longPress_.stop();
    if (pressConsumed_) {
        pressConsumed_ = false;
        event->accept();
        return;
    }
    QToolButton::mouseReleaseEvent(event);
}

void ToolFlyoutButton::paintEvent(QPaintEvent* event) {
    QToolButton::paintEvent(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const qreal right = width() - kTriangleInsetPx;
    const qreal bottom = height() - kTriangleInsetPx;
    QPainterPath triangle;
    triangle.moveTo(right, bottom - kTriangleLegPx);
    triangle.lineTo(right, bottom);
    triangle.lineTo(right - kTriangleLegPx, bottom);
    triangle.closeSubpath();
    painter.setPen(Qt::NoPen);
    painter.setBrush(design::color(isEnabled() ? design::kTextSecondary : design::kTextDisabled));
    painter.drawPath(triangle);
}

void ToolFlyoutButton::openPalette() {
    setDown(false);

    auto* popup = new QFrame(this, Qt::Popup | Qt::FramelessWindowHint);
    popup->setObjectName(QStringLiteral("toolFlyoutPopup"));
    popup->setAttribute(Qt::WA_DeleteOnClose);
    auto* layout = new QVBoxLayout(popup);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    for (QAction* variant : variants_) {
        auto* row = new QToolButton(popup);
        row->setObjectName(QStringLiteral("toolFlyoutRow"));
        row->setDefaultAction(variant);
        row->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        row->setIconSize(QSize(kPaletteIconPx, kPaletteIconPx));
        row->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        connect(row, &QToolButton::clicked, this, [this, variant, popup]() {
            if (variant->isCheckable()) setFace(variant);
            popup->close();
        });
        layout->addWidget(row);
    }

    popup->adjustSize();
    popup->move(mapToGlobal(QPoint(width(), 0)));
    popup->show();
}

}  // namespace plnr::ui
