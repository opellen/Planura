#include "rectangle_tool.h"

#include <cmath>
#include <utility>

#include <Qt>

namespace plnr::tools {

namespace {

// World-axis unit vectors, reused by every plane basis below (cases A/B/C).
constexpr geo::Vec3 kWorldX{1.0, 0.0, 0.0};
constexpr geo::Vec3 kWorldY{0.0, 1.0, 0.0};
constexpr geo::Vec3 kWorldZ{0.0, 0.0, 1.0};

// Ray/plane intersection through point along normal. nullopt if parallel
// or behind the ray origin. Generalizes groundPlaneHit() below to an
// ARBITRARY plane -- case A's face plane and case B's vertical plane use it.
std::optional<geo::Vec3> planeRayIntersect(const geo::Ray& ray, const geo::Vec3& point, const geo::Vec3& normal) {
    const double denom = geo::dot(ray.dir, normal);
    if (std::fabs(denom) < geo::kEps) return std::nullopt;
    const double t = geo::dot(point - ray.origin, normal) / denom;
    if (t <= geo::kEps) return std::nullopt;
    return ray.origin + ray.dir * t;
}

// Ray/z=0 intersection -- mirrors geo::infer()'s GroundPlane rung. Needed
// as an explicit fallback when a HIGHER-priority hit already won infer()'s
// ladder off the ground; nullopt if the ray doesn't cross z=0 ahead.
std::optional<geo::Vec3> groundPlaneHit(const geo::Ray& ray) {
    return planeRayIntersect(ray, geo::Vec3{0.0, 0.0, 0.0}, kWorldZ);
}

// resolve()'s ground-only post-processing, reused by resolveFirstCorner().
// On-ground hit keeps z snapped to 0.0; OFF-ground must NOT be flattened in
// place -- falls back to the click ray's z=0 crossing (flagged-traps-tools-A.md).
geo::Inference groundFallback(geo::Inference inf, const geo::Ray& ray) {
    if (inf.kind != geo::InferenceKind::None && std::fabs(inf.pos.z) > geo::kMergeTol) {
        const std::optional<geo::Vec3> ground = groundPlaneHit(ray);
        return ground ? geo::Inference{geo::InferenceKind::GroundPlane, *ground} : geo::Inference{};
    }
    inf.pos.z = 0.0;
    return inf;
}

// Case A's in-plane basis for a resolved face normal n: near-vertical
// normal keeps ground's u=X/v=Y; otherwise u = normalized(cross(worldZ,n)),
// v completes the right-handed triple
std::pair<geo::Vec3, geo::Vec3> faceInPlaneBasis(const geo::Vec3& n) {
    const geo::Vec3 uRaw = geo::cross(kWorldZ, n);
    if (geo::length(uRaw) < geo::kEps) {
        return {kWorldX, kWorldY};
    }
    const geo::Vec3 u = geo::normalized(uRaw);
    const geo::Vec3 v = geo::cross(n, u);
    return {u, v};
}

constexpr double kGoldenRatio = 1.6180339887;

// APPROXIMATION (eyeballed, not measured from a screenshot). Square fires
// within this fraction of a 1.0 long:short ratio; Golden Section within
// this fraction of kGoldenRatio (tolerance scales with phi).
constexpr double kProportionSnapTol = 0.04;

// the reference modeler's selection-blue (viewport_widget.cpp's kSelectionColor),
// reused for the Square/Golden Section cue's dashed trace -- restates the
// literal rather than crossing tool.h's own (different, darker-blue) boundary.
constexpr PreviewColor kProportionTraceColor{0.13f, 0.45f, 0.90f, 1.0f};

// Preview-outline border color: the reference modeler colors it by the axis
// PERPENDICULAR to the plane (a Y-Z rectangle draws RED). A Face-case
// normal counts as "axis-aligned" within this cosine (~2.5 deg)
constexpr double kAxisAlignedNormalMinCos = 0.999;

}  // namespace

void RectangleTool::onActivate(ToolContext& ctx) {
    corner1_.reset();
    corner1OnFace_ = false;
    verticalLatched_ = false;
    lastDrawPlane_ = DrawPlane::Ground;
    lastProportion_ = ProportionKind::None;
    ctx.setHint("Select first corner.");
}

void RectangleTool::onDeactivate(ToolContext& ctx) {
    corner1_.reset();
    corner1OnFace_ = false;
    verticalLatched_ = false;
    lastPlanePoint_.reset();
    lastU_ = kWorldX;
    lastV_ = kWorldY;
    lastDrawPlane_ = DrawPlane::Ground;
    lastProportion_ = ProportionKind::None;
    ctx.setPreview({}, std::nullopt);
    ctx.setInferenceCue(std::nullopt);
}

RectangleTool::ProportionSnap RectangleTool::snapProportion(const geo::Vec3& corner1, const geo::Vec3& corner2,
                                                            const geo::Vec3& u, const geo::Vec3& v,
                                                            bool shiftHeld) const {
    const geo::Vec3 d = corner2 - corner1;
    const double du = geo::dot(d, u);
    const double dv = geo::dot(d, v);
    const double adu = std::fabs(du);
    const double adv = std::fabs(dv);

    // Both axes must clear the degenerate threshold first -- avoids a
    // near-zero-denominator ratio division on a collapsed rectangle.
    if (adu < geo::kMergeTol || adv < geo::kMergeTol) {
        return {corner2, ProportionKind::None};
    }

    ProportionKind kind = ProportionKind::None;
    if (shiftHeld && lastProportion_ != ProportionKind::None) {
        // Shift LOCKS whichever proportion was last active -- keep snapping
        // to it regardless of drift until Shift releases.
        kind = lastProportion_;
    } else {
        const double ratio = std::max(adu, adv) / std::min(adu, adv);
        if (std::fabs(ratio - 1.0) < kProportionSnapTol) {
            kind = ProportionKind::Square;
        } else if (std::fabs(ratio - kGoldenRatio) < kProportionSnapTol * kGoldenRatio) {
            kind = ProportionKind::Golden;
        }
    }

    if (kind == ProportionKind::None) {
        return {corner2, ProportionKind::None};
    }

    // Snap: preserve the LARGER-magnitude axis exactly, adjust only the
    // smaller one -- Square makes it equal the larger; Golden Section makes
    // it the larger divided by phi. Axis-agnostic (du/dv along current u/v).
    double snappedDu = du;
    double snappedDv = dv;
    if (kind == ProportionKind::Square) {
        if (adu <= adv) {
            snappedDu = std::copysign(adv, du);
        } else {
            snappedDv = std::copysign(adu, dv);
        }
    } else {  // Golden
        if (adu >= adv) {
            snappedDv = std::copysign(adu / kGoldenRatio, dv);
        } else {
            snappedDu = std::copysign(adv / kGoldenRatio, du);
        }
    }

    return {corner1 + u * snappedDu + v * snappedDv, kind};
}

geo::Inference RectangleTool::resolveRaw(ToolContext& ctx, const PointerEvent& e, std::optional<geo::Vec3> anchor) const {
    const geo::Model* model = ctx.model();
    if (!model) return geo::Inference{};
    return geo::infer(*model, e.ray, geo::InferenceContext{anchor, std::nullopt, e.tols});
}

geo::Inference RectangleTool::resolve(ToolContext& ctx, const PointerEvent& e) const {
    // Case C's contract: no anchor (resolveCorner2()'s own SEPARATE anchored
    // call detects case B instead) plus the ground fallback above.
    return groundFallback(resolveRaw(ctx, e, std::nullopt), e.ray);
}

RectangleTool::FirstCornerHit RectangleTool::resolveFirstCorner(ToolContext& ctx, const PointerEvent& e) const {
    const geo::Inference raw = resolveRaw(ctx, e, std::nullopt);
    if (raw.kind == geo::InferenceKind::OnFace) {
        // Case A: kept AS-IS at the face's own position/plane rather than
        // projected down to the ground under it.
        const geo::Model* model = ctx.model();
        const geo::Face* face = model ? model->face(raw.refId) : nullptr;
        if (face) {
            return {raw, true, face->normal};
        }
        // Inconsistent model (refId no longer resolves) -- fall through to
        // the ground path rather than standing up a plane with no normal.
    }
    return {groundFallback(raw, e.ray), false, geo::Vec3{}};
}

RectangleTool::Corner2Candidate RectangleTool::resolveCorner2(ToolContext& ctx, const PointerEvent& e) {
    const geo::Vec3& c1 = *corner1_;

    if (corner1OnFace_) {
        // Case A: plane fixed to the face resolved at corner1_ --
        // generalizes groundPlaneHit() against (c1, faceNormal_).
        const std::optional<geo::Vec3> hit = planeRayIntersect(e.ray, c1, faceNormal_);
        if (!hit) return {};
        return {*hit, faceU_, faceV_, DrawPlane::Face, true};
    }

    // Ground base (cases B/C): a SEPARATE raw, anchor=c1 inference tests
    // how far off ground the cursor aims; case C's flat path below still calls resolve() unchanged.
    const geo::Inference raw = resolveRaw(ctx, e, c1);
    if (raw.kind != geo::InferenceKind::None && std::fabs(raw.pos.z - c1.z) > geo::kMergeTol) {
        // Case B engages when the un-flattened inference leaves corner1's
        // height -- LATCH it, or moving sideways for width would drop it back to ground.
        verticalLatched_ = true;
    }
    if (verticalLatched_) {
        // While latched, resolve against BOTH candidate vertical planes and
        // keep the wider horizontal extent, flipping mid-drag if needed.
        const std::optional<geo::Vec3> hitXZ = planeRayIntersect(e.ray, c1, kWorldY);  // plane spanned by X/Z
        const std::optional<geo::Vec3> hitYZ = planeRayIntersect(e.ray, c1, kWorldX);  // plane spanned by Y/Z
        const double duX = hitXZ ? std::fabs(hitXZ->x - c1.x) : -1.0;
        const double duY = hitYZ ? std::fabs(hitYZ->y - c1.y) : -1.0;
        if (duX >= 0.0 || duY >= 0.0) {
            const bool useXZ = duX >= duY;
            const geo::Vec3 hit = useXZ ? *hitXZ : *hitYZ;
            if (std::fabs(hit.z - c1.z) > geo::kMergeTol) {
                return {hit, useXZ ? kWorldX : kWorldY, kWorldZ, DrawPlane::Vertical, true};
            }
        }
        // Cursor height fell back to corner1's level (or missed both planes) -- release the latch.
        verticalLatched_ = false;
    }

    // Case C: flat diagonal -- byte-identical to resolve()'s contract.
    const geo::Inference ground = resolve(ctx, e);
    if (ground.kind == geo::InferenceKind::None) return {};
    return {ground.pos, kWorldX, kWorldY, DrawPlane::Ground, true};
}

void RectangleTool::reset(ToolContext& ctx) {
    corner1_.reset();
    corner1OnFace_ = false;
    verticalLatched_ = false;
    lastPlanePoint_.reset();
    lastU_ = kWorldX;
    lastV_ = kWorldY;
    lastDrawPlane_ = DrawPlane::Ground;
    lastProportion_ = ProportionKind::None;
    ctx.setPreview({}, std::nullopt);
    ctx.setInferenceCue(std::nullopt);
    ctx.setHint("Select first corner.");
}

void RectangleTool::onPointerMove(ToolContext& ctx, const PointerEvent& e) {
    if (!corner1_) {
        // Placing the FIRST corner: the SAME resolveFirstCorner() onPointerDown
        // uses, so the marker never disagrees with the next click. No outline yet.
        const FirstCornerHit hit = resolveFirstCorner(ctx, e);
        const bool hasTarget = hit.inf.kind != geo::InferenceKind::None;
        if (hasTarget) lastPlanePoint_ = hit.inf.pos;
        lastProportion_ = ProportionKind::None;
        ctx.setInferenceCue(std::nullopt);
        ctx.setPreview({}, hasTarget ? std::optional<geo::Vec3>(hit.inf.pos) : std::nullopt);
        ctx.setHint("Select first corner.");
        return;
    }

    const Corner2Candidate cand = resolveCorner2(ctx, e);
    if (!cand.valid) {
        lastProportion_ = ProportionKind::None;
        ctx.setInferenceCue(std::nullopt);
        ctx.setPreview({}, std::nullopt);
        ctx.setHint("Select second corner.");
        return;
    }

    lastPlanePoint_ = cand.pos;
    lastU_ = cand.u;
    lastV_ = cand.v;
    lastDrawPlane_ = cand.plane;

    const geo::Vec3& c1 = *corner1_;
    // Square/Golden Section proportion inference (u, v axes) drives the
    // outline/marker/cue below, so onPointerDown's commit never diverges.
    const ProportionSnap snap = snapProportion(c1, cand.pos, cand.u, cand.v, e.shift);
    lastProportion_ = snap.kind;
    const geo::Vec3& c2 = snap.corner2;

    // The 4-corner outline is (c1, c1+u*du, c1+u*du+v*dv, c1+v*dv) for every
    // case -- Ground's (u=X, v=Y) reduces to the original x/y math.
    const geo::Vec3 d = c2 - c1;
    const geo::Vec3 p2 = c1 + cand.u * geo::dot(d, cand.u);
    const geo::Vec3 p4 = c1 + cand.v * geo::dot(d, cand.v);

    std::vector<float> lineVerts = {
        static_cast<float>(c1.x), static_cast<float>(c1.y), static_cast<float>(c1.z),
        static_cast<float>(p2.x), static_cast<float>(p2.y), static_cast<float>(p2.z),

        static_cast<float>(p2.x), static_cast<float>(p2.y), static_cast<float>(p2.z),
        static_cast<float>(c2.x), static_cast<float>(c2.y), static_cast<float>(c2.z),

        static_cast<float>(c2.x), static_cast<float>(c2.y), static_cast<float>(c2.z),
        static_cast<float>(p4.x), static_cast<float>(p4.y), static_cast<float>(p4.z),

        static_cast<float>(p4.x), static_cast<float>(p4.y), static_cast<float>(p4.z),
        static_cast<float>(c1.x), static_cast<float>(c1.y), static_cast<float>(c1.z),
    };

    // Border color -- see kAxisAlignedNormalMinCos for the Face-case case.
    // Vertical takes the axis PERPENDICULAR to the plane; Ground stays default.
    PreviewColor borderColor = kDefaultPreviewColor;
    if (cand.plane == DrawPlane::Vertical) {
        borderColor = axisColorFor(geo::cross(cand.u, cand.v));
    } else if (cand.plane == DrawPlane::Face) {
        // A HORIZONTAL face (normal ~+-Z) falls through to default --
        // UNVERIFIED; only X/Y-dominant face normals qualify.
        if (std::fabs(faceNormal_.x) >= kAxisAlignedNormalMinCos || std::fabs(faceNormal_.y) >= kAxisAlignedNormalMinCos) {
            borderColor = axisColorFor(faceNormal_);
        }
    }

    if (snap.kind == ProportionKind::None) {
        ctx.setInferenceCue(std::nullopt);
    } else {
        // Dashed blue diagonal from corner1 to the snapped corner, same
        // trace mechanism as FromPoint's cue, built by hand (no geo::Inference here).
        InferenceCue cue;
        cue.pos = c2;
        cue.shape = kMarkerNone;
        cue.screenTip = snap.kind == ProportionKind::Square ? "Square" : "Golden Section";
        cue.traceFrom = c1;
        cue.traceColor = kProportionTraceColor;
        ctx.setInferenceCue(cue);
    }
    // setPreviewBatches (not setPreview) so the outline carries borderColor
    // -- both target the SAME overlay, a safe swap.
    ctx.setPreviewBatches({{std::move(lineVerts), borderColor.r, borderColor.g, borderColor.b, borderColor.a}}, c2);

    ctx.setHint("Select second corner.");
}

void RectangleTool::onPointerDown(ToolContext& ctx, const PointerEvent& e) {
    if (!corner1_) {
        // The SAME classification onPointerMove's preview used, so the
        // placed corner never disagrees with what was previewed.
        const FirstCornerHit hit = resolveFirstCorner(ctx, e);
        if (hit.inf.kind == geo::InferenceKind::None) return;

        corner1_ = hit.inf.pos;
        corner1OnFace_ = hit.onFace;
        if (hit.onFace) {
            faceNormal_ = hit.normal;
            const std::pair<geo::Vec3, geo::Vec3> basis = faceInPlaneBasis(faceNormal_);
            faceU_ = basis.first;
            faceV_ = basis.second;
        }
        return;
    }

    const Corner2Candidate cand = resolveCorner2(ctx, e);
    if (!cand.valid) return;

    const geo::Vec3& c1 = *corner1_;
    // The SAME snap onPointerMove's preview showed -- committing cand.pos
    // unsnapped would let the rectangle diverge from the preview.
    const ProportionSnap snap = snapProportion(c1, cand.pos, cand.u, cand.v, e.shift);
    const geo::Vec3& c2 = snap.corner2;
    lastProportion_ = snap.kind;

    const geo::Vec3 d = c2 - c1;
    const double du = geo::dot(d, cand.u);
    const double dv = geo::dot(d, cand.v);
    if (std::fabs(du) < geo::kMergeTol || std::fabs(dv) < geo::kMergeTol) {
        // Degenerate rectangle (zero width or height) -- ignore, stay armed
        // at the first corner.
        return;
    }

    if (cand.plane == DrawPlane::Ground) {
        // Case C: the EXISTING path, unchanged -- nothing downstream
        // changes for the common ground-plane case.
        ctx.requestAddRectangle(c1, c2);
    } else {
        // Cases A/B: not axis-aligned ground -- go through the SAME 4-point
        // closed polyline every shape tool funnels through.
        const geo::Vec3 p2 = c1 + cand.u * du;
        const geo::Vec3 p4 = c1 + cand.v * dv;
        ctx.requestAddPolyline({c1, p2, c2, p4}, /*closed=*/true);
    }
    reset(ctx);
}

void RectangleTool::onVcbCommit(ToolContext& ctx, const VcbValue& value) {
    if (!corner1_ || !lastPlanePoint_ || value.kind != VcbValue::Kind::Dims2) {
        // Scalar (the reference modeler wants a dims pair here, not a single length) and
        // every other Kind, plus no corner placed yet, all land here.
        ctx.setHint("Invalid entry.");
        return;
    }
    const double da = std::fabs(value.a);
    const double db = std::fabs(value.b);
    if (da <= geo::kMergeTol || db <= geo::kMergeTol) {
        ctx.setHint("Invalid entry.");
        return;
    }

    const geo::Vec3& c1 = *corner1_;
    // Signs come from the drag quadrant (lastPlanePoint_ vs corner1_) dotted
    // against lastU_/lastV_ -- for Ground this is the original x/y comparison.
    const geo::Vec3 lastDelta = *lastPlanePoint_ - c1;
    const double signU = geo::dot(lastDelta, lastU_) >= 0.0 ? 1.0 : -1.0;
    const double signV = geo::dot(lastDelta, lastV_) >= 0.0 ? 1.0 : -1.0;
    const double du = signU * da;
    const double dv = signV * db;
    const geo::Vec3 corner2 = c1 + lastU_ * du + lastV_ * dv;

    if (lastDrawPlane_ == DrawPlane::Ground) {
        // Same requestAddRectangle call as onPointerDown's commit -- only
        // corner2's source differs (computed here, picked there).
        ctx.requestAddRectangle(c1, corner2);
    } else {
        const geo::Vec3 p2 = c1 + lastU_ * du;
        const geo::Vec3 p4 = c1 + lastV_ * dv;
        ctx.requestAddPolyline({c1, p2, corner2, p4}, /*closed=*/true);
    }
    reset(ctx);
}

void RectangleTool::onKeyDown(ToolContext& ctx, int key, bool /*ctrl*/) {
    if (key != Qt::Key_Escape) return;
    reset(ctx);
}

}  // namespace plnr::tools
