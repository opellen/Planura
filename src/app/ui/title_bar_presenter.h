#pragma once

#include <ordo/core/kernel.h>
#include <ordo/qt/presenter.h>

#include "agent/events.h"

class QMainWindow;

namespace plnr::ui {

// Mirrors DocumentStore's identity/dirty state onto MainWindow's title bar:
// "<name or Untitled>[*] - Planura", via QMainWindow's "[*]" placeholder mechanism. Given only
// the QMainWindow it updates.
class TitleBarPresenter : public ordo::qt::Presenter {
public:
    TitleBarPresenter(QMainWindow* window);

    // Subscribes to DocumentStateChanged and does an initial refresh() so
    // pre-registration DocumentStore state still shows up.
    void onRegister() override;

private:
    void onDocumentStateChanged(const events::DocumentStateChanged& event);
    void refresh();

    QMainWindow* window_;
};

}  // namespace plnr::ui
