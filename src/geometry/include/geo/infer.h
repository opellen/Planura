#pragma once

#include <optional>
#include <utility>
#include <vector>

#include <geo/model.h>
#include <geo/pick.h>

namespace plnr::geo {

enum class InferenceKind {
    None,
    Endpoint,
    Midpoint,
    OnEdge,
    OnFace,
    Intersection,
    GuidePoint,
    GuideLine,
    FromPoint,
    Parallel,
    Perpendicular,
    OnAxis,
    GroundPlane,
};

// A locked inference axis: dir must already be unit length.
struct AxisLock {
    Vec3 origin;
    Vec3 dir;
};

// An app-owned infinite guide line, passed to infer() by value; the kernel never owns guides.
// dir must already be unit length.
struct GuideLineData {
    Vec3 point;
    Vec3 dir;
    Id id;
};

// An app-owned guide point, passed to infer() by value.
struct GuidePointData {
    Vec3 pos;
    Id id;
};

// Caller-owned state for a single infer() call; infer() itself is stateless. Every field beyond
// anchor/axisLock/tols defaults to empty, and any step that would consume an empty one is skipped.
struct InferenceContext {
    std::optional<Vec3> anchor;
    std::optional<AxisLock> axisLock;
    PickOptions tols;

    // Hover-charged points; each contributes 3 candidate axis-aligned lines (FromPoint).
    std::vector<Vec3> chargedAnchors;

    // (pointA, pointB) the Parallel/Perpendicular directions derive from; needs anchor set too.
    std::optional<std::pair<Vec3, Vec3>> referenceEdge;

    // Guide geometry, passed by value from the app's GuideStore.
    std::vector<GuideLineData> guideLines;
    std::vector<GuidePointData> guidePoints;

    // Axis directions the FromPoint and automatic OnAxis rungs test against (world X/Y/Z unless the
    // app relocates the drawing axes). The LOCKED OnAxis rung uses ctx.axisLock instead.
    std::array<Vec3, 3> axisDirs{Vec3{1.0, 0.0, 0.0}, Vec3{0.0, 1.0, 0.0}, Vec3{0.0, 0.0, 1.0}};
};

// Resolved inference; field meaning varies by kind. pos is always set ({} for None). refId names the
// resolved entity where there is one (vertex, edge, face, guide), kInvalidId otherwise. dir is the
// unit line/axis direction for the line kinds. source is FromPoint's charged anchor.
struct Inference {
    InferenceKind kind = InferenceKind::None;
    Vec3 pos;
    Id refId = kInvalidId;
    std::optional<Vec3> dir;
    std::optional<Vec3> source;
};

// Resolves the ray to a single inferred point. Steps run in strict priority order and the first
// candidate wins; a step whose ctx data is absent contributes nothing and is skipped.

// Order: Endpoint, Midpoint, Intersection, GuidePoint (all within tols.vertexTol) -> OnEdge,
// GuideLine (tols.edgeTol) -> OnFace -> locked OnAxis (no tolerance) -> FromPoint -> Parallel,
// Perpendicular -> auto OnAxis -> GroundPlane -> None.

// Auto OnAxis gates each axis on the aim's SCREEN direction within a 12-degree cone, not its world
// direction, and rejects |t| <= tols.edgeTol (cursor still on the anchor).
Inference infer(const Model& model, const Ray& ray, const InferenceContext& ctx);

}  // namespace plnr::geo
