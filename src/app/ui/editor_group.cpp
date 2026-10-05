#include "ui/editor_group.h"

#include <QContextMenuEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QStackedWidget>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

#include "constants/design_tokens.h"
#include "document_session.h"
#include "viewport/viewport_widget.h"

namespace plnr::ui {

namespace {

constexpr int kCloseButtonSize = 16;  // guide's legibility floor
constexpr int kCloseGlyphInset = 4;   // stroke endpoints sit this far from the button edge
constexpr qreal kCloseStrokeWidth = 1.5;

// Tab close button. The stock close glyph ignores QSS, so the X is painted
// with two strokes in a text role (design-grammar §8).
class CloseButton : public QToolButton {
public:
    explicit CloseButton(QWidget* parent) : QToolButton(parent) {
        setAutoRaise(true);
        setFixedSize(kCloseButtonSize, kCloseButtonSize);
        setToolTip(QStringLiteral("Close"));
        setFocusPolicy(Qt::NoFocus);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        const bool hovered = isEnabled() && underMouse();
        if (hovered) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(design::color(design::kSurfaceHover));
            painter.drawRoundedRect(rect(), design::kRadius, design::kRadius);
        }
        const char* role = !isEnabled() ? design::kTextDisabled
                                        : (hovered ? design::kTextPrimary : design::kTextSecondary);
        QPen pen(design::color(role), kCloseStrokeWidth);
        pen.setCapStyle(Qt::RoundCap);
        painter.setPen(pen);
        const int lo = kCloseGlyphInset;
        const int hi = kCloseButtonSize - kCloseGlyphInset;
        painter.drawLine(lo, lo, hi, hi);
        painter.drawLine(lo, hi, hi, lo);
    }
};

}  // namespace

// ---- EditorTabBar -----------------------------------------------------------

EditorTabBar::EditorTabBar(QWidget* parent) : QTabBar(parent) {
    setObjectName(QStringLiteral("editorTabBar"));
    // Custom per-tab close buttons, not setTabsClosable(): see CloseButton.
    setMovable(true);
    setExpanding(false);
    setDrawBase(false);
    setElideMode(Qt::ElideRight);
    setUsesScrollButtons(true);
}

void EditorTabBar::contextMenuEvent(QContextMenuEvent* event) {
    const int index = tabAt(event->pos());
    if (index >= 0) {
        emit tabContextMenuRequested(index, event->globalPos());
        event->accept();
        return;
    }
    QTabBar::contextMenuEvent(event);
}

void EditorTabBar::mousePressEvent(QMouseEvent* event) {
    emit barPressed();
    if (tabAt(event->pos()) < 0) {
        emit barBackgroundClicked();  // empty strip area: a focus request, nothing more
    }
    QTabBar::mousePressEvent(event);
}

// ---- EditorGroup ------------------------------------------------------------

EditorGroup::EditorGroup(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("editorGroup"));
    // A plain QWidget paints no QSS border without this; the focus ring needs it.
    setAttribute(Qt::WA_StyledBackground, true);

    tabBar_ = new EditorTabBar(this);
    stack_ = new QStackedWidget(this);

    emptyHint_ = new QLabel(QStringLiteral("No document open"), this);
    emptyHint_->setAlignment(Qt::AlignCenter);
    emptyHint_->setStyleSheet(design::resolveRoles(
        QStringLiteral("color: {text-disabled}; background: {surface-0}; font-size: %1px;").arg(design::kTypeSizePx)));
    stack_->addWidget(emptyHint_);

    // Zero margins and spacing: with the strip hidden the stack, and so the
    // page and viewport, fill the group exactly.
    layout_ = new QVBoxLayout(this);
    layout_->setContentsMargins(0, 0, 0, 0);
    layout_->setSpacing(0);
    layout_->addWidget(tabBar_);
    layout_->addWidget(stack_, 1);

    connect(tabBar_, &QTabBar::currentChanged, this, [this](int) {
        syncStackToCurrent();
        emit currentChanged(currentSession());
    });
    // QTabBar already moved its own tab from -> to before this fires.
    connect(tabBar_, &QTabBar::tabMoved, this, [this](int from, int to) {
        Tab moved = tabs_[static_cast<std::size_t>(from)];
        tabs_.erase(tabs_.begin() + from);
        tabs_.insert(tabs_.begin() + to, moved);
    });
    connect(tabBar_, &EditorTabBar::tabContextMenuRequested, this, &EditorGroup::tabContextMenuRequested);
    connect(tabBar_, &EditorTabBar::barBackgroundClicked, this, &EditorGroup::barBackgroundClicked);
    connect(tabBar_, &EditorTabBar::barPressed, this, &EditorGroup::focusRequested);

    syncStackToCurrent();
}

void EditorGroup::addSessionTab(DocumentSession& session, const QString& title) {
    auto* page = new QWidget();
    auto* pageLayout = new QVBoxLayout(page);
    pageLayout->setContentsMargins(0, 0, 0, 0);
    pageLayout->setSpacing(0);
    // One-time reparent into the page; never reparent after show().
    pageLayout->addWidget(session.viewport());

    Tab tab;
    tab.session = &session;
    tab.page = page;
    tab.title = title;
    tab.isCustom = false;
    tabs_.push_back(tab);
    stack_->addWidget(page);
    const int index = tabBar_->addTab(title);
    addCloseButton(index, page);
    tabBar_->setCurrentIndex(index);
    syncStackToCurrent();  // no-op if setCurrentIndex already fired; covers the first-tab case
    refreshCloseButtons();
}

void EditorGroup::setSessionTabTitle(DocumentSession* session, const QString& title) {
    const int index = indexOf(session);
    if (index < 0) {
        return;
    }
    tabs_[static_cast<std::size_t>(index)].title = title;
    tabBar_->setTabText(index, title);
}

QWidget* EditorGroup::openCustomTab(QWidget* widget, const QString& title) {
    Tab tab;
    tab.page = widget;
    tab.title = title;
    tab.isCustom = true;
    tabs_.push_back(tab);
    stack_->addWidget(widget);
    const int index = tabBar_->addTab(title);
    addCloseButton(index, widget);
    tabBar_->setCurrentIndex(index);
    syncStackToCurrent();
    return widget;
}

void EditorGroup::addCloseButton(int tabIndex, QWidget* page) {
    auto* button = new CloseButton(tabBar_);
    // Looked up by page on click: drag-reorder and closes shift indices.
    connect(button, &QToolButton::clicked, this, [this, page] {
        const int i = indexOfPage(page);
        if (i >= 0) {
            closeTab(i);
        }
    });
    tabBar_->setTabButton(tabIndex, QTabBar::RightSide, button);
}

void EditorGroup::closeTab(int index) {
    if (index < 0 || index >= count()) {
        return;
    }
    const Tab tab = tabs_[static_cast<std::size_t>(index)];
    if (!tab.isCustom) {
        if (!sessionClosable()) {
            return;  // the last session tab cannot be closed
        }
        emit tabCloseRequested(tab.session);
        return;
    }

    // tabs_ first, so the currentChanged that removeTab() fires sees an
    // index-aligned vector.
    tabs_.erase(tabs_.begin() + index);
    tabBar_->removeTab(index);
    stack_->removeWidget(tab.page);
    tab.page->deleteLater();
    if (tabs_.empty()) {
        syncStackToCurrent();
    }
    refreshCloseButtons();
}

void EditorGroup::removeSessionTab(DocumentSession* session) {
    const int index = indexOf(session);
    if (index < 0) {
        return;
    }
    const Tab tab = tabs_[static_cast<std::size_t>(index)];
    // tabs_ first, as in closeTab(): removeTab() may fire currentChanged.
    tabs_.erase(tabs_.begin() + index);
    tabBar_->removeTab(index);
    stack_->removeWidget(tab.page);
    // Immediate, not deleteLater(): the viewport must be gone before the caller
    // destroys the session whose tool host references it.
    delete tab.page;
    if (tabs_.empty()) {
        syncStackToCurrent();
    }
    refreshCloseButtons();
}

void EditorGroup::setCurrentSession(DocumentSession* session) {
    const int index = indexOf(session);
    if (index >= 0) {
        tabBar_->setCurrentIndex(index);
    }
}

int EditorGroup::indexOf(const DocumentSession* session) const {
    if (!session) {
        return -1;
    }
    for (std::size_t i = 0; i < tabs_.size(); ++i) {
        if (tabs_[i].session == session) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

DocumentSession* EditorGroup::currentSession() const {
    const int index = tabBar_->currentIndex();
    return sessionAt(index);
}

DocumentSession* EditorGroup::sessionAt(int index) const {
    if (index < 0 || index >= count()) {
        return nullptr;
    }
    return tabs_[static_cast<std::size_t>(index)].session;
}

void EditorGroup::setTabStripVisible(bool visible) {
    tabBar_->setVisible(visible);
}

int EditorGroup::indexOfPage(const QWidget* page) const {
    for (std::size_t i = 0; i < tabs_.size(); ++i) {
        if (tabs_[i].page == page) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int EditorGroup::sessionTabCount() const {
    int n = 0;
    for (const Tab& tab : tabs_) {
        n += tab.isCustom ? 0 : 1;
    }
    return n;
}

bool EditorGroup::sessionClosable() const {
    return closeGuard_ ? closeGuard_() : sessionTabCount() > 1;
}

void EditorGroup::setFocused(bool focused) {
    if (property("focused").toBool() == focused) {
        return;
    }
    setProperty("focused", focused);
    style()->unpolish(this);
    style()->polish(this);
    update();
}

void EditorGroup::setRingReserved(bool reserved) {
    const int margin = reserved ? design::kHairline : 0;
    layout_->setContentsMargins(margin, margin, margin, margin);
}

QString EditorGroup::tabTitle(const DocumentSession* session) const {
    const int index = indexOf(session);
    return index >= 0 ? tabs_[static_cast<std::size_t>(index)].title : QString();
}

void EditorGroup::refreshCloseButtons() {
    // Last-session-tab rule: its close button is disabled (painted in text-disabled).
    const bool closable = sessionClosable();
    for (int i = 0; i < count(); ++i) {
        if (!tabs_[static_cast<std::size_t>(i)].isCustom) {
            if (QWidget* button = tabBar_->tabButton(i, QTabBar::RightSide)) {
                button->setEnabled(closable);
            }
        }
    }
}

void EditorGroup::syncStackToCurrent() {
    const int index = tabBar_->currentIndex();
    if (index < 0 || index >= count()) {
        stack_->setCurrentWidget(emptyHint_);
        return;
    }
    stack_->setCurrentWidget(tabs_[static_cast<std::size_t>(index)].page);
}

}  // namespace plnr::ui
