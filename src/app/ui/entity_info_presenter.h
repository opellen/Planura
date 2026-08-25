#pragma once

#include <QLabel>
#include <QObject>

#include <ordo/core/app_kernel.h>
#include <ordo/qt/presenter.h>

#include "agent/events.h"

namespace plnr::ui {

// Mirrors the current selection onto the Entity Info section's summary
// label: single-entity selections show the entity kind plus a measurement
// (position/length/area), multi-entity selections show a kind breakdown, and
// an empty selection shows "No Selection". Measurement math lives in
// geo::measure -- this presenter only formats.
class EntityInfoPresenter : public ordo::qt::Presenter {
public:
    EntityInfoPresenter(ordo::core::AppKernel& kernel, QLabel* infoLabel);

    // Subscribes to SelectionChanged, GeometryChanged (a geometry edit can
    // change or prune the selected entity's measurement), and
    // MaterialsChanged (a paint/edit can change the material line), then
    // does an initial render so a pre-existing selection still shows up.
    // Called by the owner (MainWindow) once the whole window tree is built.
    void onRegister() override;

private:
    void onSelectionChanged(const events::SelectionChanged& event);
    void onGeometryChanged(const events::GeometryChanged& event);
    void onMaterialsChanged(const events::MaterialsChanged& event);

    // Re-reads SelectionStore + GeometryApi and reformats infoLabel_'s
    // text. Refs the model no longer knows about (stale between a prune and
    // this render) are skipped defensively.
    void render();

    QLabel* infoLabel_;
};

}  // namespace plnr::ui
