#pragma once

#include <ordo/core/kernel.h>
#include <ordo/qt/presenter.h>

#include "agent/events.h"

class QAction;

namespace plnr::ui {

// Mirrors UndoStore's canUndo()/canRedo() onto the Edit menu's Undo/Redo QActions' enabled
// flags: subscribes to events::UndoStateChanged (an empty Fact) and re-pulls UndoStore.
// Given only the two QActions.
class UndoMenuPresenter : public ordo::qt::Presenter {
public:
    UndoMenuPresenter(QAction* undoAction, QAction* redoAction);

    // Subscribes to UndoStateChanged and does an initial refresh(), so the pre-existing (empty)
    // state is reflected immediately.
    void onRegister() override;

private:
    void onUndoStateChanged(const events::UndoStateChanged& event);
    void refresh();

    QAction* undoAction_;
    QAction* redoAction_;
};

}  // namespace plnr::ui
