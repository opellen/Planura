#include "status_bar_presenter.h"

namespace plnr::ui {

namespace {

// industry-standard prompt shown as soon as a tool becomes active; tools narrow
// this further via StatusHintChanged as the interaction progresses.
QString promptFor(events::ToolId tool) {
    switch (tool) {
        case events::ToolId::Select:
            return QStringLiteral("Select entities.");
        case events::ToolId::Line:
            return QStringLiteral("Select start point.");
        case events::ToolId::Eraser:
            return QStringLiteral("Click or drag over entities to erase.");
        case events::ToolId::Move:
            return QStringLiteral("Select entities to move.");
        case events::ToolId::Rectangle:
            return QStringLiteral("Select first corner.");
        case events::ToolId::PushPull:
            return QStringLiteral("Select a face to push or pull.");
    }
    return QString();
}

}  // namespace

StatusBarPresenter::StatusBarPresenter(QLabel* hintLabel, QLabel* vcbLabel,
                                        QLineEdit* vcbEdit)
    : Presenter(QStringLiteral("StatusBarPresenter"), hintLabel),
      hintLabel_(hintLabel),
      vcbLabel_(vcbLabel),
      vcbEdit_(vcbEdit) {}

void StatusBarPresenter::onRegister() {
    subscribe<events::ToolChanged>(&StatusBarPresenter::onToolChanged);
    subscribe<events::StatusHintChanged>(&StatusBarPresenter::onStatusHintChanged);
    subscribe<events::VcbLabelChanged>(&StatusBarPresenter::onVcbLabelChanged);
    subscribe<events::VcbValueChanged>(&StatusBarPresenter::onVcbValueChanged);
}

void StatusBarPresenter::onToolChanged(const events::ToolChanged& event) {
    hintLabel_->setText(promptFor(event.tool));
}

void StatusBarPresenter::onStatusHintChanged(const events::StatusHintChanged& event) {
    hintLabel_->setText(QString::fromStdString(event.hint));
}

void StatusBarPresenter::onVcbLabelChanged(const events::VcbLabelChanged& event) {
    vcbLabel_->setText(QString::fromStdString(event.label));
}

void StatusBarPresenter::onVcbValueChanged(const events::VcbValueChanged& event) {
    // Live readouts must never fight a typed entry: hasFocus() covers real
    // typing; vcbEntryActive (see MainWindow::vcbEntryActive_) covers
    // bridge-driven typing while the window is inactive.
    if (vcbEdit_->hasFocus() || vcbEdit_->property("vcbEntryActive").toBool()) return;
    vcbEdit_->setText(QString::fromStdString(event.text));
}

}  // namespace plnr::ui
