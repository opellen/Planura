#pragma once

#include <QLabel>
#include <QLineEdit>
#include <QObject>

#include <ordo/core/app_kernel.h>
#include <ordo/qt/presenter.h>

#include "agent/events.h"

namespace plnr::ui {

// Mirrors ToolChanged/StatusHintChanged kernel events onto the status bar's
// hint label, and VcbLabelChanged/VcbValueChanged onto the VCB
// (Measurements Box)'s label and field. Given only the widgets it must
// update -- never reaches back into MainWindow for anything else.
class StatusBarPresenter : public ordo::qt::Presenter {
public:
    StatusBarPresenter(ordo::core::AppKernel& kernel, QLabel* hintLabel, QLabel* vcbLabel, QLineEdit* vcbEdit);

    // Subscribes to the kernel. Called by the owner (MainWindow) once
    // construction of the whole window tree is finished.
    void onRegister() override;

private:
    void onToolChanged(const events::ToolChanged& event);
    void onStatusHintChanged(const events::StatusHintChanged& event);
    void onVcbLabelChanged(const events::VcbLabelChanged& event);
    // Sets vcbEdit_'s text ONLY while it lacks keyboard focus -- a live
    // readout must never fight the user's own typing (matches the reference modeler:
    // typing mid-interaction isn't overwritten by the live readout).
    void onVcbValueChanged(const events::VcbValueChanged& event);

    QLabel* hintLabel_;
    QLabel* vcbLabel_;
    QLineEdit* vcbEdit_;
};

}  // namespace plnr::ui
