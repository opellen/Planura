#pragma once

#include <QLabel>
#include <QLineEdit>
#include <QObject>

#include <ordo/core/kernel.h>
#include <ordo/qt/presenter.h>

#include "agent/events.h"

namespace plnr::ui {

// Mirrors ToolChanged/StatusHintChanged onto the status bar's hint label, and
// VcbLabelChanged/VcbValueChanged onto the VCB label and field. Given only the widgets it updates.
class StatusBarPresenter : public ordo::qt::Presenter {
public:
    StatusBarPresenter(QLabel* hintLabel, QLabel* vcbLabel, QLineEdit* vcbEdit);

    // Subscribes to the kernel. Called by the owner (MainWindow) once the window tree is built.
    void onRegister() override;

private:
    void onToolChanged(const events::ToolChanged& event);
    void onStatusHintChanged(const events::StatusHintChanged& event);
    void onVcbLabelChanged(const events::VcbLabelChanged& event);
    // Sets vcbEdit_'s text ONLY while it lacks keyboard focus: a live readout must never fight
    // the user's own typing.
    void onVcbValueChanged(const events::VcbValueChanged& event);

    QLabel* hintLabel_;
    QLabel* vcbLabel_;
    QLineEdit* vcbEdit_;
};

}  // namespace plnr::ui
