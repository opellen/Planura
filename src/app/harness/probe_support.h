#pragma once

// Helpers shared by the harness translation units.

#include <QPointF>
#include <QString>
#include <Qt>

#include <optional>

#include <geo/model.h>

namespace ordo::core {
class Kernel;
}

namespace plnr::viewport {
class ViewportWidget;
}

// Reports one assertion: `PASS: <name>` on stdout, `FAIL: <name> -- <detail>` on stderr.
// Names are the probe's PASS-set, so keep each distinct and stable. Returns `ok`.
bool probeCheck(const char* name, bool ok, const QString& detail = {});

// Pumps the event loop for `ms` of wall-clock time; the only waiting primitive (no retries, no sleeps).
void pumpEventsFor(int ms);

// <repo>/temp/captures/planura-probe/, created on demand.
QString probeCaptureDir();

// World -> widget-pixel projection (debug bridge's cmdProject). `visible` = in front of the camera
// and inside the viewport on every NDC axis; assert it before clicking.
struct ProjectedPoint {
    QPointF widget;
    bool visible{false};
};
ProjectedPoint projectWorld(plnr::viewport::ViewportWidget* viewport, const plnr::geo::Vec3& world);

// Left press+release at the projected point; false (nothing sent) if not visible. A click within
// ToolController's double-click distance of the previous one first pumps past doubleClickInterval().
bool clickWorld(plnr::viewport::ViewportWidget* viewport, const plnr::geo::Vec3& world,
                Qt::KeyboardModifiers modifiers = {});

// Hover move to the projected point; false if not visible.
bool moveWorld(plnr::viewport::ViewportWidget* viewport, const plnr::geo::Vec3& world,
               Qt::KeyboardModifiers modifiers = {});

// Intentional double click: press, release, double-click press, release, back to back.
bool doubleClickWorld(plnr::viewport::ViewportWidget* viewport, const plnr::geo::Vec3& world,
                      Qt::KeyboardModifiers modifiers = {});

// Entity coordinates from the kernel's GeometryApi model; nullopt for an unknown id.
std::optional<plnr::geo::Vec3> worldOfVertex(ordo::core::Kernel& kernel, plnr::geo::Id id);
std::optional<plnr::geo::Vec3> worldOfEdgeMidpoint(ordo::core::Kernel& kernel, plnr::geo::Id id);
std::optional<plnr::geo::Vec3> worldOfFaceCentroid(ordo::core::Kernel& kernel, plnr::geo::Id id);
