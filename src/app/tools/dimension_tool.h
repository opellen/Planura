#pragma once

#include <optional>
#include <string>
#include <utility>

#include <geo/entity.h>
#include <geo/infer.h>
#include <geo/pick.h>
#include <geo/vec3.h>

#include "agent/events.h"
#include "tool.h"

namespace plnr::tools {

// industry-standard Dimension tool. Three clicks, stage machine: PickA (the
// full inference ladder runs for hover cues, then snaps down to the
// nearest model VERTEX -- this app anchors dimensions only to actual
// vertices, never a bare midpoint/on-edge point; no radius/diameter).

// PickB: same resolve-then-snap, must land on a different vertex. Place-
// Offset: every move intersects the ray against the plane through the A-B
// midpoint (normal = A-B direction), so offsetDir_/offset_ fall out with
// no separate projection step. Click 3 commits and returns to PickA.

// Rendered label is |A-B| (VCB mirrors it during PlaceOffset); no typed
// VCB override is supported. Escape (any stage) cancels back to PickA.
class DimensionTool : public Tool {
public:
    void onActivate(ToolContext& ctx) override;
    void onDeactivate(ToolContext& ctx) override;
    void onPointerMove(ToolContext& ctx, const PointerEvent& e) override;
    void onPointerDown(ToolContext& ctx, const PointerEvent& e) override;
    void onKeyDown(ToolContext& ctx, int key, bool ctrl) override;

private:
    enum class Stage { PickA, PickB, PlaceOffset };

    // Full inference ladder (model + guides, no anchor), so hover cues
    // show normally even though this tool ultimately snaps to a vertex.
    geo::Inference resolve(ToolContext& ctx, const PointerEvent& e) const;

    // model->findVertex(inf.pos, vertexTol) -- nullopt if inf itself is None
    // or no vertex qualifies within tolerance.
    std::optional<std::pair<geo::Id, geo::Vec3>> nearestVertex(const geo::Model* model, const geo::Inference& inf,
                                                                 double vertexTol) const;

    // A stable perpendicular-to-dir fallback, seeded at click 2 so
    // PlaceOffset's preview is sane before the first pointer move.
    geo::Vec3 defaultPerpDir(const geo::Vec3& dir) const;

    // Recomputes offsetDir_/offset_ from e and rebuilds the extension-line/
    // dimension-line/tick preview, setting the VCB to the fixed |A-B|
    // distance.
    void updateOffsetPreview(ToolContext& ctx, const PointerEvent& e);

    void resetToIdle(ToolContext& ctx);
    std::string currentHint() const;

    Stage stage_ = Stage::PickA;

    geo::Id vertexA_{};
    geo::Id vertexB_{};
    geo::Vec3 posA_;
    geo::Vec3 posB_;

    // PickB's hover preview target (last resolved-and-snapped vertex
    // position, so the rubber-band from posA_ doesn't flicker on a miss).
    std::optional<geo::Vec3> hoverB_;

    geo::Vec3 offsetDir_{0.0, 0.0, 1.0};
    double offset_ = 0.0;
};

}  // namespace plnr::tools
