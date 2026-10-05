#pragma once

#include <QLabel>
#include <QObject>

#include <ordo/core/kernel.h>
#include <ordo/qt/presenter.h>

#include "agent/events.h"

namespace plnr::ui {

// Mirrors the current selection onto the Entity Info summary label: single entity = kind plus
// a measurement, multi = kind breakdown, empty = "No Selection". Measurement math lives in
// geo::measure; this presenter only formats.
class EntityInfoPresenter : public ordo::qt::Presenter {
public:
    EntityInfoPresenter(QLabel* infoLabel);

    // Subscribes to SelectionChanged, GeometryChanged and MaterialsChanged, then does an initial
    // render so a pre-existing selection shows. Called by the owner (MainWindow) once the window
    // tree is built.
    void onRegister() override;

private:
    void onSelectionChanged(const events::SelectionChanged& event);
    void onGeometryChanged(const events::GeometryChanged& event);
    void onMaterialsChanged(const events::MaterialsChanged& event);

    // Re-reads SelectionStore + GeometryApi and reformats infoLabel_'s text; stale refs are skipped.
    void render();

    QLabel* infoLabel_;
};

}  // namespace plnr::ui
