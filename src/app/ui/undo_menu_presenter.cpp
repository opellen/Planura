#include "ui/undo_menu_presenter.h"

#include <QAction>

#include "agent/undo_store.h"

namespace plnr::ui {

UndoMenuPresenter::UndoMenuPresenter(ordo::core::AppKernel& kernel, QAction* undoAction, QAction* redoAction)
    : Presenter(kernel, QStringLiteral("UndoMenuPresenter"), undoAction),
      undoAction_(undoAction),
      redoAction_(redoAction) {}

void UndoMenuPresenter::onRegister() {
    subscribe<events::UndoStateChanged>(&UndoMenuPresenter::onUndoStateChanged);
    refresh();
}

void UndoMenuPresenter::onUndoStateChanged(const events::UndoStateChanged& /*event*/) {
    refresh();
}

void UndoMenuPresenter::refresh() {
    auto undo = kernel().agentAs<agent::UndoStore>(agent::kUndoStoreName);
    if (!undo) return;  // No UndoStore registered -- leave whatever enabled state was already set.

    if (undoAction_) undoAction_->setEnabled(undo->canUndo());
    if (redoAction_) redoAction_->setEnabled(undo->canRedo());
}

}  // namespace plnr::ui
