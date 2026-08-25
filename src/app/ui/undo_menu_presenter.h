#pragma once

#include <ordo/core/app_kernel.h>
#include <ordo/qt/presenter.h>

#include "agent/events.h"

class QAction;

namespace plnr::ui {

// Mirrors UndoStore's canUndo()/canRedo() state onto the Edit menu's
// Undo/Redo QActions' enabled flags: subscribes to events::UndoStateChanged
// (an empty Fact) and re-pulls UndoStore rather than reading a payload --
// see that event's own comment in events.h. Given only the two QActions it
// must update -- never reaches back into MainWindow for anything else, same
// boundary TitleBarPresenter/StatusBarPresenter keep.
class UndoMenuPresenter : public ordo::qt::Presenter {
public:
    UndoMenuPresenter(ordo::core::AppKernel& kernel, QAction* undoAction, QAction* redoAction);

    // Subscribes to UndoStateChanged and does an initial refresh() so
    // pre-registration UndoStore state (both stacks empty on a fresh
    // launch) is reflected immediately, matching MainWindow's disabled
    // placeholder.
    void onRegister() override;

private:
    void onUndoStateChanged(const events::UndoStateChanged& event);
    void refresh();

    QAction* undoAction_;
    QAction* redoAction_;
};

}  // namespace plnr::ui
