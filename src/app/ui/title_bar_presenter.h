#pragma once

#include <ordo/core/app_kernel.h>
#include <ordo/qt/presenter.h>

#include "agent/events.h"

class QMainWindow;

namespace plnr::ui {

// Mirrors DocumentStore's identity/dirty state onto MainWindow's title bar:
// "<name or Untitled>[*] - Planura", via QMainWindow's own
// setWindowTitle()/setWindowModified()/setWindowFilePath() "[*]"-placeholder
// mechanism. Given only the QMainWindow it must update -- never reaches
// back into MainWindow for anything else, same boundary
// StatusBarPresenter/ViewportPresenter keep.
class TitleBarPresenter : public ordo::qt::Presenter {
public:
    TitleBarPresenter(ordo::core::AppKernel& kernel, QMainWindow* window);

    // Subscribes to DocumentStateChanged and does an initial refresh() so
    // pre-registration DocumentStore state still shows up.
    void onRegister() override;

private:
    void onDocumentStateChanged(const events::DocumentStateChanged& event);
    void refresh();

    QMainWindow* window_;
};

}  // namespace plnr::ui
