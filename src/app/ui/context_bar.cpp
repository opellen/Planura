#include "context_bar.h"

#include <QAction>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QSignalBlocker>
#include <QSize>
#include <QSizePolicy>
#include <QSpinBox>
#include <QToolButton>

namespace plnr::ui {

namespace {

constexpr int kIconPx = 24;
// Equals the height the retired standard toolbar occupied; the harness viewport
// framebuffer (2160x1215) depends on it.
constexpr int kBarHeight = 35;
constexpr int kMinSegments = 3;
constexpr int kMaxSegments = 999;

}  // namespace

ContextBar::ContextBar(QWidget* parent) : QToolBar(QStringLiteral("Context"), parent) {
    setObjectName(QStringLiteral("contextBar"));
    setMovable(false);
    setFloatable(false);
    setIconSize(QSize(kIconPx, kIconPx));
    setFixedHeight(kBarHeight);

    toolIcon_ = new QLabel(this);
    toolIcon_->setFixedSize(kIconPx, kIconPx);
    toolName_ = new QLabel(this);
    toolName_->setObjectName(QStringLiteral("contextBarToolName"));
    toolIconAction_ = addWidget(toolIcon_);
    toolNameAction_ = addWidget(toolName_);
    toolSeparatorAction_ = addSeparator();

    segmentsBox_ = new QWidget(this);
    auto* boxLayout = new QHBoxLayout(segmentsBox_);
    boxLayout->setContentsMargins(4, 0, 4, 0);
    boxLayout->setSpacing(6);
    boxLayout->addWidget(new QLabel(QStringLiteral("Sides"), segmentsBox_));
    segmentsSpin_ = new QSpinBox(segmentsBox_);
    segmentsSpin_->setRange(kMinSegments, kMaxSegments);
    // Commit on Enter/focus-out/arrows, not per keystroke: each commit is a
    // retro-edit of the last shape, same as a typed "Ns".
    segmentsSpin_->setKeyboardTracking(false);
    boxLayout->addWidget(segmentsSpin_);
    segmentsAction_ = addWidget(segmentsBox_);
    segmentsAction_->setVisible(false);
    connect(segmentsSpin_, &QSpinBox::valueChanged, this, &ContextBar::segmentsEdited);

    auto* stretch = new QWidget(this);
    stretch->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    addWidget(stretch);

    unitsLabel_ = new QLabel(this);
    unitsLabel_->setObjectName(QStringLiteral("contextBarUnits"));
    addWidget(unitsLabel_);

    perspectiveButton_ = new QToolButton(this);
    perspectiveButton_->setToolButtonStyle(Qt::ToolButtonTextOnly);
    parallelButton_ = new QToolButton(this);
    parallelButton_->setToolButtonStyle(Qt::ToolButtonTextOnly);
    addWidget(perspectiveButton_);
    addWidget(parallelButton_);
}

void ContextBar::setProjectionActions(QAction* perspective, QAction* parallel) {
    perspectiveButton_->setDefaultAction(perspective);
    parallelButton_->setDefaultAction(parallel);
}

void ContextBar::setToolFace(const QIcon& icon, const QString& name) {
    toolIcon_->setPixmap(icon.pixmap(QSize(kIconPx, kIconPx), devicePixelRatioF()));
    toolName_->setText(name);
}

void ContextBar::setMode(ShellMode mode) {
    mode_ = mode;
    const bool tools = mode == ShellMode::Edit;
    toolIconAction_->setVisible(tools);
    toolNameAction_->setVisible(tools);
    toolSeparatorAction_->setVisible(tools);
    segmentsAction_->setVisible(tools && segmentsWanted_);
}

void ContextBar::setSegmentsVisible(bool visible) {
    segmentsWanted_ = visible;
    segmentsAction_->setVisible(visible && mode_ == ShellMode::Edit);
}

void ContextBar::setSegmentsValue(int value) {
    const QSignalBlocker blocker(segmentsSpin_);
    segmentsSpin_->setValue(value);
}

int ContextBar::segmentsValue() const {
    return segmentsSpin_->value();
}

bool ContextBar::segmentsShown() const {
    return segmentsAction_->isVisible();
}

QString ContextBar::toolName() const {
    return toolName_->text();
}

QString ContextBar::unitsText() const {
    return unitsLabel_->text();
}

void ContextBar::setUnits(const QString& units) {
    unitsLabel_->setText(QStringLiteral("Units: ") + units);
}

std::optional<int> ContextBar::lastSegments(events::ToolId tool) const {
    const auto it = segmentsCache_.find(tool);
    if (it == segmentsCache_.end()) return std::nullopt;
    return it->second;
}

}  // namespace plnr::ui
