#include "ui/section_header.h"

#include <QFont>
#include <QPainter>
#include <QPainterPath>
#include <QPen>

#include "constants/design_tokens.h"
#include "ui/icons.h"

namespace plnr::ui {

namespace {

constexpr qreal kStrokeWidth = 1.6;  // chevron and plus stroke (design-grammar section 5)
constexpr int kChevronHalfSpan = 4;  // ~8px chevron span
constexpr int kPlusHalfSpan = 5;

QPen strokePen(const QColor& color) {
    return QPen(color, kStrokeWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
}

}  // namespace

PlusGlyphButton::PlusGlyphButton(QWidget* parent) : QToolButton(parent) {
    setFixedSize(design::kRowGutterSlot, design::kRowGutterSlot);
    setCursor(Qt::PointingHandCursor);
    setAutoRaise(true);
}

void PlusGlyphButton::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    const bool live = isEnabled();
    if (live && isDown()) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(design::color(design::kSurfacePressed));
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.drawRoundedRect(rect(), design::kRadius, design::kRadius);
    } else if (live && underMouse()) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(design::color(design::kSurfaceHover));
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.drawRoundedRect(rect(), design::kRadius, design::kRadius);
    }
    const QColor glyph = !live ? design::color(design::kTextDisabled)
                         : underMouse() ? design::color(design::kTextPrimary)
                                        : design::color(design::kTextSecondary);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(strokePen(glyph));
    const QPointF c(width() / 2.0, height() / 2.0);
    painter.drawLine(QPointF(c.x() - kPlusHalfSpan, c.y()), QPointF(c.x() + kPlusHalfSpan, c.y()));
    painter.drawLine(QPointF(c.x(), c.y() - kPlusHalfSpan), QPointF(c.x(), c.y() + kPlusHalfSpan));
}

SectionHeader::SectionHeader(const QString& title, const QString& iconSlug, bool collapsible, QWidget* parent)
    : QAbstractButton(parent), title_(title), iconSlug_(iconSlug), collapsible_(collapsible) {
    setAttribute(Qt::WA_Hover, true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    if (collapsible_) {
        setCursor(Qt::PointingHandCursor);
    }
    setText(displayTitle());
    QFont f = font();
    f.setPixelSize(design::kTypeSizePx);
    f.setWeight(QFont::DemiBold);
    setFont(f);
}

QString SectionHeader::displayTitle() const {
    return count_ >= 0 ? QStringLiteral("%1 (%2)").arg(title_).arg(count_) : title_;
}

void SectionHeader::setCount(int count) {
    count_ = count;
    setText(displayTitle());
    update();
}

void SectionHeader::setContent(QWidget* content) {
    content_ = content;
    if (content_ != nullptr) {
        content_->setVisible(expanded_ || !collapsible_);
    }
}

void SectionHeader::setExpanded(bool expanded) {
    if (!collapsible_ || expanded == expanded_) return;
    expanded_ = expanded;
    if (content_ != nullptr) {
        content_->setVisible(expanded_);
    }
    update();
}

void SectionHeader::nextCheckState() {
    setExpanded(!expanded_);
}

void SectionHeader::addHeaderAction(QToolButton* action) {
    action->setParent(this);
    action->show();
    actions_.push_back(action);
    placeActions();
    update();
}

QSize SectionHeader::sizeHint() const {
    return QSize(design::kSectionTitleX * 2, design::kRowGutterSlot + 2 * design::kSectionHeaderPad + design::kHairline);
}

QSize SectionHeader::minimumSizeHint() const {
    return QSize(0, sizeHint().height());
}

void SectionHeader::resizeEvent(QResizeEvent* event) {
    QAbstractButton::resizeEvent(event);
    placeActions();
}

void SectionHeader::placeActions() {
    // Right-most action sits kSpace4 left of the chevron's center; further actions stack leftwards.
    const int band = height() - design::kHairline;
    const int cy = design::kHairline + band / 2;
    int right = collapsible_ ? width() - design::kSectionChevronRightInset - design::kSpace4
                             : width() - design::kSectionChevronRightInset + design::kRowGutterSlot / 2;
    for (QToolButton* action : actions_) {
        QRect slot(QPoint(0, 0), action->size());
        slot.moveCenter(QPoint(0, cy));
        slot.moveRight(right);
        action->setGeometry(slot);
        right -= action->width();
    }
}

void SectionHeader::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    const bool hovered = isEnabled() && underMouse() && collapsible_;
    if (hovered) {
        painter.fillRect(rect(), design::color(design::kSurfaceHover));
    }
    // Hairline filled as a rect inside the header: a 1px pen centered on y=0 vanishes at 150% DPI.
    painter.fillRect(QRect(0, 0, width(), design::kHairline), design::color(design::kOutlineStrong));

    const QRect band = rect().adjusted(0, design::kHairline, 0, 0);
    const int cy = band.center().y();
    const QColor titleColor = design::color(isEnabled() ? design::kTextPrimary : design::kTextDisabled);

    int textX = design::kSpace2;
    if (!iconSlug_.isEmpty()) {
        const QPixmap icon = icons::pixmap(iconSlug_, design::kSectionIconSize, devicePixelRatioF());
        painter.drawPixmap(design::kSectionIconX, cy - design::kSectionIconSize / 2, icon);
        textX = design::kSectionTitleX;
    }

    int textRight = width() - design::kSpace2;
    if (!actions_.isEmpty()) {
        textRight = actions_.last()->x() - design::kSpace2;
    } else if (collapsible_) {
        textRight = width() - design::kSectionChevronRightInset - kChevronHalfSpan - design::kSpace2;
    }
    painter.setFont(font());
    painter.setPen(titleColor);
    painter.drawText(QRect(textX, band.top(), qMax(0, textRight - textX), band.height()),
                     Qt::AlignLeft | Qt::AlignVCenter, text());

    if (collapsible_) {
        const QColor chevronColor = !isEnabled() ? design::color(design::kTextDisabled)
                                    : hovered    ? design::color(design::kTextPrimary)
                                                 : design::color(design::kTextSecondary);
        const qreal cx = width() - design::kSectionChevronRightInset;
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setPen(strokePen(chevronColor));
        QPainterPath arrow;
        if (expanded_) {  // down chevron
            arrow.moveTo(cx - kChevronHalfSpan, cy - 2);
            arrow.lineTo(cx, cy + 2.5);
            arrow.lineTo(cx + kChevronHalfSpan, cy - 2);
        } else {  // right chevron
            arrow.moveTo(cx - 2, cy - kChevronHalfSpan);
            arrow.lineTo(cx + 2.5, cy);
            arrow.lineTo(cx - 2, cy + kChevronHalfSpan);
        }
        painter.drawPath(arrow);
    }
}

}  // namespace plnr::ui
