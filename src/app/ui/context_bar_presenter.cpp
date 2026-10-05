#include "context_bar_presenter.h"

#include <QAction>
#include <QIcon>
#include <QString>

#include "ui/context_bar.h"

namespace plnr::ui {

ContextBarPresenter::ContextBarPresenter(ContextBar* bar, ActionResolver resolveAction)
    : Presenter(QStringLiteral("ContextBarPresenter"), bar),
      bar_(bar),
      resolveAction_(std::move(resolveAction)) {}

void ContextBarPresenter::onRegister() {
    subscribe<events::ToolChanged>(&ContextBarPresenter::onToolChanged);
    subscribe<events::ToolSegmentsChanged>(&ContextBarPresenter::onToolSegmentsChanged);
    connect(bar_, &ContextBar::segmentsEdited, this, [this](int segments) {
        context().send(events::ToolSegmentsRequested{activeTool_, segments});
    });
    // No ToolChanged precedes the first session's idle Select state.
    onToolChanged(events::ToolChanged{activeTool_});
}

void ContextBarPresenter::onToolChanged(const events::ToolChanged& event) {
    activeTool_ = event.tool;
    const QAction* action = resolveAction_ ? resolveAction_(event.tool) : nullptr;
    bar_->setToolFace(action ? action->icon() : QIcon(), action ? action->text() : QString());
    bar_->setSegmentsVisible(false);
}

void ContextBarPresenter::onToolSegmentsChanged(const events::ToolSegmentsChanged& event) {
    bar_->rememberSegments(event.tool, event.segments);
    if (event.tool != activeTool_) return;
    bar_->setSegmentsValue(event.segments);
    bar_->setSegmentsVisible(true);
}

}  // namespace plnr::ui
