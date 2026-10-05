#include "section_plane_tool.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <utility>

#include <Qt>

#include <geo/model.h>

#include "constants/design_fidelity.h"

namespace plnr::tools {

namespace {

// Ray ∩ plane(point, normal); nullopt when parallel or behind the ray origin.
std::optional<geo::Vec3> rayPlaneIntersect(const geo::Ray& ray, const geo::Vec3& point, const geo::Vec3& normal) {
    const double denom = geo::dot(ray.dir, normal);
    if (std::fabs(denom) < geo::kEps) return std::nullopt;
    const double t = geo::dot(point - ray.origin, normal) / denom;
    if (t <= geo::kEps) return std::nullopt;
    return ray.origin + ray.dir * t;
}

// Deterministic orthonormal in-plane basis (u, v) for `normal` -- needed by
// both the rectangle corners and the double-click hit-test below.
std::pair<geo::Vec3, geo::Vec3> planeBasis(const geo::Vec3& normal) {
    const geo::Vec3 helper = std::fabs(normal.z) > 0.9 ? geo::Vec3{1.0, 0.0, 0.0} : geo::Vec3{0.0, 0.0, 1.0};
    const geo::Vec3 uRaw = geo::cross(helper, normal);
    const double uLen = geo::length(uRaw);
    const geo::Vec3 u = uLen > geo::kEps ? uRaw * (1.0 / uLen) : geo::Vec3{1.0, 0.0, 0.0};
    const geo::Vec3 v = geo::cross(normal, u);  // already unit -- normal, u are unit and mutually perpendicular
    return {u, v};
}

// Bounding box of every vertex in the model (no selection concept here,
// unlike FlipTool). valid is false for a null/empty model.
struct Bbox {
    geo::Vec3 min, max;
    bool valid{};
};

Bbox modelBbox(const geo::Model* model) {
    Bbox box;
    if (!model) return box;
    bool first = true;
    for (const auto& [id, v] : model->vertices()) {
        (void)id;
        if (first) {
            box.min = box.max = v.pos;
            first = false;
        } else {
            box.min.x = std::min(box.min.x, v.pos.x);
            box.min.y = std::min(box.min.y, v.pos.y);
            box.min.z = std::min(box.min.z, v.pos.z);
            box.max.x = std::max(box.max.x, v.pos.x);
            box.max.y = std::max(box.max.y, v.pos.y);
            box.max.z = std::max(box.max.z, v.pos.z);
        }
    }
    box.valid = !first;
    return box;
}

// Margin beyond the bbox extent: 20% of the diagonal, floored at 0.3 world
// units. Falls back to the floor alone when the model is empty/absent.
double margin(const Bbox& box) {
    constexpr double kFraction = 0.2;
    constexpr double kMinMargin = 0.3;
    if (!box.valid) return kMinMargin;
    return std::max(geo::distance(box.min, box.max) * kFraction, kMinMargin);
}

// Half-extent of the (square) plane rectangle: half the bbox diagonal,
// used uniformly for both u,v since the plane's orientation is arbitrary
// (can overshoot the model's true silhouette; MVP simplification).
double planeHalfExtent(const Bbox& box) {
    constexpr double kEmptyModelHalfExtent = 3.0;
    if (!box.valid) return kEmptyModelHalfExtent + margin(box);
    return geo::distance(box.min, box.max) * 0.5 + margin(box);
}

void appendVertex(std::vector<float>& verts, const geo::Vec3& v) {
    verts.push_back(static_cast<float>(v.x));
    verts.push_back(static_cast<float>(v.y));
    verts.push_back(static_cast<float>(v.z));
}

void appendSegment(std::vector<float>& verts, const geo::Vec3& a, const geo::Vec3& b) {
    appendVertex(verts, a);
    appendVertex(verts, b);
}

// Outlined rectangle + small L-bracket corner-grip ticks -- UNVERIFIED
// against the reference modeler (whose grips read as small squares).
void appendPlaneRectangle(std::vector<float>& verts, const geo::Vec3& center, const geo::Vec3& u, const geo::Vec3& v,
                           double half) {
    const geo::Vec3 uOff = u * half;
    const geo::Vec3 vOff = v * half;
    const geo::Vec3 c1 = center + uOff + vOff;
    const geo::Vec3 c2 = center - uOff + vOff;
    const geo::Vec3 c3 = center - uOff - vOff;
    const geo::Vec3 c4 = center + uOff - vOff;
    appendSegment(verts, c1, c2);
    appendSegment(verts, c2, c3);
    appendSegment(verts, c3, c4);
    appendSegment(verts, c4, c1);

    constexpr double kGripFraction = 0.12;  // fraction of half-extent each grip tick's arm reaches
    const double grip = half * kGripFraction;
    auto appendGrip = [&](const geo::Vec3& corner, const geo::Vec3& intoU, const geo::Vec3& intoV) {
        appendSegment(verts, corner, corner + intoU * grip);
        appendSegment(verts, corner, corner + intoV * grip);
    };
    appendGrip(c1, -u, -v);
    appendGrip(c2, u, -v);
    appendGrip(c3, u, v);
    appendGrip(c4, -u, v);
}

// Light semi-transparent green, matching the reference modeler's section-plane fill --
// UNVERIFIED exact RGBA (no screenshot source).
constexpr PreviewColor kCandidateColor{design::kCanvasSectionCandidate.r, design::kCanvasSectionCandidate.g,
                                       design::kCanvasSectionCandidate.b, design::kCanvasSectionCandidate.a};

}  // namespace

SectionPlaneTool::PlaneHit SectionPlaneTool::resolveHover(ToolContext& ctx, const PointerEvent& e) const {
    const geo::PickResult hit = ctx.pick(e, e.tols);

    if (hit.kind == geo::PickKind::Face) {
        const geo::Model* model = ctx.model();
        const geo::Face* face = model ? model->face(hit.id) : nullptr;
        geo::Vec3 normal{0.0, 0.0, 1.0};
        if (face && geo::length(face->normal) > geo::kEps) {
            normal = geo::normalized(face->normal);
        }
        return PlaneHit{hit.point, normal, true};
    }

    if (hit.kind != geo::PickKind::None) {
        // Vertex/Edge hit: keep the hit point but fall back to the
        // ground-plane normal (no face to derive an orientation from).
        return PlaneHit{hit.point, geo::Vec3{0.0, 0.0, 1.0}, true};
    }

    const std::optional<geo::Vec3> ground = rayPlaneIntersect(e.ray, geo::Vec3{0.0, 0.0, 0.0}, geo::Vec3{0.0, 0.0, 1.0});
    if (!ground) return PlaneHit{geo::Vec3{}, geo::Vec3{0.0, 0.0, 1.0}, false};
    return PlaneHit{*ground, geo::Vec3{0.0, 0.0, 1.0}, true};
}

SectionPlaneTool::PlaneHit SectionPlaneTool::currentCandidate(ToolContext& ctx, const PointerEvent& e) {
    if (e.shift && !shiftHeld_) {
        // Rising edge: capture the freeze, only if the current hover is
        // itself valid (else shiftLock_ stays unset, falling through to plain hover).
        const PlaneHit hover = resolveHover(ctx, e);
        if (hover.valid) shiftLock_ = hover;
    } else if (!e.shift) {
        shiftLock_.reset();
    }
    shiftHeld_ = e.shift;

    if (shiftLock_ && shiftLock_->valid) {
        const std::optional<geo::Vec3> onLocked = rayPlaneIntersect(e.ray, shiftLock_->point, shiftLock_->normal);
        return PlaneHit{onLocked.value_or(shiftLock_->point), shiftLock_->normal, true};
    }

    const PlaneHit hover = resolveHover(ctx, e);
    if (!hover.valid) return hover;
    if (orientationOverride_) {
        return PlaneHit{hover.point, *orientationOverride_, true};
    }
    return hover;
}

bool SectionPlaneTool::tryToggleExisting(ToolContext& ctx, const PointerEvent& e) {
    const Bbox box = modelBbox(ctx.model());
    const double half = planeHalfExtent(box);

    // Excludes lastPlacedId_ on purpose: press 1 of this gesture may have
    // just placed a plane on top of a pre-existing one, still findable here.
    geo::Id bestId = geo::kInvalidId;
    bool bestActive = false;
    double bestT = std::numeric_limits<double>::max();

    for (const SectionPlaneData& plane : ctx.sections()) {
        if (plane.hidden) continue;  // a hidden plane's outline isn't clickable -- nothing to double-click
        if (plane.id == lastPlacedId_) continue;
        const double denom = geo::dot(e.ray.dir, plane.normal);
        if (std::fabs(denom) < geo::kEps) continue;  // ray parallel to this plane
        const double t = geo::dot(plane.point - e.ray.origin, plane.normal) / denom;
        if (t <= geo::kEps) continue;  // behind the camera

        const geo::Vec3 hitPoint = e.ray.origin + e.ray.dir * t;
        const auto [u, v] = planeBasis(plane.normal);
        const double du = geo::dot(hitPoint - plane.point, u);
        const double dv = geo::dot(hitPoint - plane.point, v);
        if (std::fabs(du) > half || std::fabs(dv) > half) continue;  // outside this plane's own rectangle

        if (t < bestT) {  // nearest-hit tie-break
            bestT = t;
            bestId = plane.id;
            bestActive = plane.active;
        }
    }

    bool handled = false;

    // lastPlacedId_ set means press 1 of this gesture placed a plane a
    // moment ago; a trusted double-click undoes it unconditionally.
    if (lastPlacedId_ != geo::kInvalidId) {
        ctx.requestRemoveSectionPlane(lastPlacedId_);
        lastPlacedId_ = geo::kInvalidId;
        handled = true;
    }

    // The pre-existing plane the click also hit (if any) toggles normally,
    // on top of undoing press 1's own spurious placement.
    if (bestId != geo::kInvalidId) {
        ctx.requestSetSectionActive(bestId, !bestActive);
        handled = true;
    }

    return handled;
}

void SectionPlaneTool::commit(ToolContext& ctx) {
    if (!candidate_.valid) return;

    // Best-effort suggestion only (no access to the Agent's real id counter);
    // a cancelled/cleared prompt falls back to SectionStore's own auto-name.
    const std::string suggested = "Section Plane " + std::to_string(ctx.sections().size() + 1);
    const std::optional<std::string> typed = ctx.promptText("Section Plane Name", suggested);
    // Cancel yields nullopt here, which becomes an empty name -- the Agent
    // auto-names it exactly as if the field were left blank.
    ctx.requestAddSectionPlane(candidate_.point, candidate_.normal, typed.value_or(std::string()));

    // Dispatch already completed synchronously -- find the new plane as the
    // max-id entry (ids are monotonic) and activate it.
    geo::Id newId = geo::kInvalidId;
    for (const SectionPlaneData& plane : ctx.sections()) {
        if (plane.id > newId) newId = plane.id;
    }
    if (newId != geo::kInvalidId) ctx.requestSetSectionActive(newId, true);

    // Remembered for tryToggleExisting: a trusted double-click's second
    // press undoes THIS press's own placement via this id, not a toggle.
    lastPlacedId_ = newId;

    ctx.setHint("Click a face to place a Section Plane.");
}

std::vector<ToolContext::PreviewBatch> SectionPlaneTool::buildPreview() const {
    std::vector<ToolContext::PreviewBatch> batches;
    if (!candidate_.valid) return batches;

    const auto [u, v] = planeBasis(candidate_.normal);
    std::vector<float> verts;
    appendPlaneRectangle(verts, candidate_.point, u, v, candidatePreviewHalf_);
    batches.push_back(
        ToolContext::PreviewBatch{std::move(verts), kCandidateColor.r, kCandidateColor.g, kCandidateColor.b, kCandidateColor.a});
    return batches;
}

void SectionPlaneTool::onActivate(ToolContext& ctx) {
    candidate_ = PlaneHit{};
    shiftLock_.reset();
    orientationOverride_.reset();
    shiftHeld_ = false;
    hasPressedSinceActivate_ = false;
    lastPlacedId_ = geo::kInvalidId;
    ctx.setPreviewBatches({}, std::nullopt);
    ctx.setHint("Click a face to place a Section Plane.");
}

void SectionPlaneTool::onDeactivate(ToolContext& ctx) {
    candidate_ = PlaneHit{};
    shiftLock_.reset();
    orientationOverride_.reset();
    shiftHeld_ = false;
    hasPressedSinceActivate_ = false;
    lastPlacedId_ = geo::kInvalidId;
    ctx.setPreviewBatches({}, std::nullopt);
}

void SectionPlaneTool::onPointerMove(ToolContext& ctx, const PointerEvent& e) {
    candidate_ = currentCandidate(ctx, e);
    candidatePreviewHalf_ = planeHalfExtent(modelBbox(ctx.model()));
    ctx.setPreviewBatches(buildPreview(), std::nullopt);
}

void SectionPlaneTool::onPointerDown(ToolContext& ctx, const PointerEvent& e) {
    // clickCount >= 2 on this tool's own first press since activation can
    // only be inherited noise -- falls through to ordinary placement.
    const bool trustClickCount = hasPressedSinceActivate_;
    hasPressedSinceActivate_ = true;

    if (e.clickCount >= 2 && trustClickCount) {
        if (tryToggleExisting(ctx, e)) return;
        // A doubled click that missed every plane's rectangle is swallowed, not placed.
        return;
    }

    candidate_ = currentCandidate(ctx, e);
    commit(ctx);
}

void SectionPlaneTool::onKeyDown(ToolContext& ctx, int key, bool /*ctrl*/) {
    switch (key) {
        case Qt::Key_Escape:
            shiftLock_.reset();
            orientationOverride_.reset();
            return;
        case Qt::Key_Up:
            orientationOverride_ = ctx.axesFrame().zDir;  // blue
            return;
        case Qt::Key_Right:
            orientationOverride_ = ctx.axesFrame().xDir;  // red
            return;
        case Qt::Key_Left:
            orientationOverride_ = ctx.axesFrame().yDir;  // green
            return;
        case Qt::Key_Down:
            orientationOverride_.reset();  // re-follow the hovered face's own normal
            return;
        default:
            return;
    }
}

}  // namespace plnr::tools
