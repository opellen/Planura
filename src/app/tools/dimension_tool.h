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

// Dimension: PickA and PickB run the full inference ladder for hover cues, then snap to the nearest model
// VERTEX (never a bare midpoint/on-edge point; B must differ from A). PlaceOffset: the ray hits the plane
// through the A-B midpoint (normal = A-B), so offsetDir_/offset_ need no projection step; click 3
// commits. Label is |A-B| (VCB mirrors it); no typed override. Escape cancels to PickA.
class DimensionTool : public Tool {
public:
    void onActivate(ToolContext& ctx) override;
    void onDeactivate(ToolContext& ctx) override;
    void onPointerMove(ToolContext& ctx, const PointerEvent& e) override;
    void onPointerDown(ToolContext& ctx, const PointerEvent& e) override;
    void onKeyDown(ToolContext& ctx, int key, bool ctrl) override;

private:
    enum class Stage { PickA, PickB, PlaceOffset };

    // Full inference ladder (model + guides, no anchor) for hover cues; the tool then snaps to a vertex.
    geo::Inference resolve(ToolContext& ctx, const PointerEvent& e) const;

    // model->findVertex(inf.pos, vertexTol); nullopt if inf is None or no vertex is within tolerance.
    std::optional<std::pair<geo::Id, geo::Vec3>> nearestVertex(const geo::Model* model, const geo::Inference& inf,
                                                                 double vertexTol) const;

    // Stable perpendicular-to-dir fallback, seeded at click 2 so the PlaceOffset preview is sane before the first move.
    geo::Vec3 defaultPerpDir(const geo::Vec3& dir) const;

    // Recomputes offsetDir_/offset_ from e, rebuilds the extension/dimension-line/tick preview, VCB = fixed |A-B|.
    void updateOffsetPreview(ToolContext& ctx, const PointerEvent& e);

    void resetToIdle(ToolContext& ctx);
    std::string currentHint() const;

    Stage stage_ = Stage::PickA;

    geo::Id vertexA_{};
    geo::Id vertexB_{};
    geo::Vec3 posA_;
    geo::Vec3 posB_;

    // PickB's hover target (last snapped vertex), so the rubber-band from posA_ doesn't flicker on a miss.
    std::optional<geo::Vec3> hoverB_;

    geo::Vec3 offsetDir_{0.0, 0.0, 1.0};
    double offset_ = 0.0;
};

}  // namespace plnr::tools
