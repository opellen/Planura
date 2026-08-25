#include "context_menu.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

#include <geo/model.h>

#include "agent/events.h"

namespace plnr::ui {

namespace {

// Closest point on the infinite line (origin, dir; must be unit) to ray.
struct LineHit {
    geo::Vec3 pointOnLine;
    double distToRay{};
};

LineHit closestPointOnLineToRay(const geo::Vec3& origin, const geo::Vec3& dir, const geo::Ray& ray) {
    const geo::Vec3 r = ray.origin - origin;
    const double b = geo::dot(ray.dir, dir);
    const double c = geo::dot(ray.dir, r);
    const double f = geo::dot(dir, r);
    const double denom = 1.0 - b * b;  // dir, ray.dir both unit length

    geo::Vec3 pointOnLine;
    if (std::fabs(denom) < geo::kEps) {
        pointOnLine = origin;  // near-parallel: fall back to the line's own origin
    } else {
        const double s = (f - b * c) / denom;
        pointOnLine = origin + dir * s;
    }
    const double t = std::max(0.0, geo::dot(pointOnLine - ray.origin, ray.dir));
    const geo::Vec3 closestOnRay = ray.origin + ray.dir * t;
    return {pointOnLine, geo::distance(pointOnLine, closestOnRay)};
}

// Distance from a fixed point to the ray; t < 0 (behind camera) never qualifies.
double distancePointToRay(const geo::Vec3& pos, const geo::Ray& ray) {
    const double t = geo::dot(pos - ray.origin, ray.dir);
    if (t < 0.0) return std::numeric_limits<double>::max();
    const geo::Vec3 closest = ray.origin + ray.dir * t;
    return geo::distance(closest, pos);
}

// Model bbox + section-plane rectangle half-extent: half the bbox diagonal
// plus a 20%-of-diagonal margin (floored at 0.3 units). Must match
// SectionPlaneTool/ViewportPresenter's own copy of this formula.
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

double sectionMargin(const Bbox& box) {
    constexpr double kFraction = 0.2;
    constexpr double kMinMargin = 0.3;
    if (!box.valid) return kMinMargin;
    return std::max(geo::distance(box.min, box.max) * kFraction, kMinMargin);
}

double sectionHalfExtent(const Bbox& box) {
    constexpr double kEmptyModelHalfExtent = 3.0;
    if (!box.valid) return kEmptyModelHalfExtent + sectionMargin(box);
    return geo::distance(box.min, box.max) * 0.5 + sectionMargin(box);
}

// Arbitrary but deterministic orthonormal in-plane basis for `normal`.
std::pair<geo::Vec3, geo::Vec3> planeBasis(const geo::Vec3& normal) {
    const geo::Vec3 helper = std::fabs(normal.z) > 0.9 ? geo::Vec3{1.0, 0.0, 0.0} : geo::Vec3{0.0, 0.0, 1.0};
    const geo::Vec3 uRaw = geo::cross(helper, normal);
    const double uLen = geo::length(uRaw);
    const geo::Vec3 u = uLen > geo::kEps ? uRaw * (1.0 / uLen) : geo::Vec3{1.0, 0.0, 0.0};
    const geo::Vec3 v = geo::cross(normal, u);  // already unit -- normal, u are unit and mutually perpendicular
    return {u, v};
}

// Shared by "Outer Shell" and the 5 flattened "Solid Tools" entries below --
// selection-based, not pick-based like the other items in this file.
std::function<void()> makeSolidOpAction(tools::ToolContext& ctx, events::SolidOp op) {
    return [&ctx, op]() {
        // Refs that aren't solid Instance operands (loose geometry, or an
        // Instance whose Definition fails geo::isSolidDefinition).
        std::vector<events::EntityRef> offenders;
        for (const events::EntityRef& ref : ctx.selection()) {
            if (ref.kind != geo::EntityKind::Instance) {
                offenders.push_back(ref);
                continue;
            }
            const tools::ToolContext::SolidTargetInfo info = ctx.solidTargetInfo(ref.id);
            if (!info.valid || !ctx.isDefinitionSolid(info.definitionId)) offenders.push_back(ref);
        }

        if (!offenders.empty()) {
            // Matches the reference modeler's dialog text; reuses ctx.confirm()'s
            // Yes/No modal (true = proceed) rather than a new OK/Cancel type.
            const bool ok = ctx.confirm(
                "One or more of the selected objects is either not a solid or is locked. The non-solid/locked "
                "objects will be de-selected before this operation is performed.");
            if (!ok) return;  // Cancel -- nothing changes

            for (const events::EntityRef& offender : offenders) {
                ctx.requestSelect(events::SelectMode::Subtract, offender, events::SelectExpand::None);
            }
        }

        // Selection (click) order -- cutter/target order for Subtract/Trim.
        std::vector<geo::Id> ids;
        for (const events::EntityRef& ref : ctx.selection()) {
            if (ref.kind == geo::EntityKind::Instance) ids.push_back(ref.id);
        }
        if (ids.size() < 2) {
            // Exact-arity checks happen in GeometryApi::applySolidOp, not here.
            ctx.setHint("Not enough solid objects remain to complete this operation.");
            return;
        }
        ctx.requestSolidOp(op, std::move(ids));
    };
}

}  // namespace

std::vector<ContextMenuItem> buildContextMenu(tools::ToolContext& ctx, ordo::core::AppKernel& kernel,
                                               const tools::PointerEvent& e,
                                               const std::function<void(geo::Id)>& startDivide,
                                               const std::function<void(geo::Id)>& startPositionTexture) {
    std::vector<ContextMenuItem> items;

    // >= 2 selected Instances -> Solid Tools for the whole selection. No
    // submenu support, so flattened into 6 items (order matches the Tools
    // menu). Solidity is checked lazily inside makeSolidOpAction.
    {
        std::size_t instanceCount = 0;
        for (const events::EntityRef& ref : ctx.selection()) {
            if (ref.kind == geo::EntityKind::Instance) ++instanceCount;
        }
        if (instanceCount >= 2) {
            items.push_back(ContextMenuItem{"Outer Shell", false, false, makeSolidOpAction(ctx, events::SolidOp::OuterShell)});
            items.push_back(
                ContextMenuItem{"Solid Tools > Union", false, false, makeSolidOpAction(ctx, events::SolidOp::Union)});
            items.push_back(ContextMenuItem{
                "Solid Tools > Intersect", false, false, makeSolidOpAction(ctx, events::SolidOp::Intersect)});
            items.push_back(ContextMenuItem{
                "Solid Tools > Subtract", false, false, makeSolidOpAction(ctx, events::SolidOp::Subtract)});
            items.push_back(
                ContextMenuItem{"Solid Tools > Trim", false, false, makeSolidOpAction(ctx, events::SolidOp::Trim)});
            items.push_back(
                ContextMenuItem{"Solid Tools > Split", false, false, makeSolidOpAction(ctx, events::SolidOp::Split)});
            return items;
        }
    }

    // Edge hit only (vertex/face picking disabled).
    const geo::PickResult edgeHit = ctx.pick(e, geo::PickOptions{0.0, e.tols.edgeTol});
    if (edgeHit.kind == geo::PickKind::Edge) {
        const geo::Id edgeId = edgeHit.id;
        items.push_back(ContextMenuItem{
            "Divide", false, false, [startDivide, edgeId]() {
                if (startDivide) startDivide(edgeId);
            }});
        items.push_back(ContextMenuItem{
            "Erase", false, false,
            [&kernel, edgeId]() { kernel.send(events::RemoveEdgeRequested{edgeId}); }});
        return items;
    }

    // Guides aren't in pick(), hit-tested directly; point beats line on a tie.
    {
        geo::Id bestId = geo::kInvalidId;
        double bestDist = e.tols.vertexTol;
        for (const geo::GuidePointData& g : ctx.guidePoints()) {
            const double d = distancePointToRay(g.pos, e.ray);
            if (d <= bestDist) {
                bestDist = d;
                bestId = g.id;
            }
        }
        if (bestId == geo::kInvalidId) {
            double bestLineDist = e.tols.edgeTol;
            for (const geo::GuideLineData& g : ctx.guideLines()) {
                const LineHit hit = closestPointOnLineToRay(g.point, g.dir, e.ray);
                if (hit.distToRay <= bestLineDist) {
                    bestLineDist = hit.distToRay;
                    bestId = g.id;
                }
            }
        }
        if (bestId != geo::kInvalidId) {
            const geo::Id guideId = bestId;
            items.push_back(ContextMenuItem{
                "Erase", false, false,
                [&kernel, guideId]() { kernel.send(events::EraseGuideRequested{guideId}); }});
            items.push_back(ContextMenuItem{
                "Hide", false, false,
                [&kernel, guideId]() { kernel.send(events::SetGuideHiddenRequested{guideId, true}); }});
            items.push_back(ContextMenuItem{
                "Delete All Guides", false, false,
                [&kernel]() { kernel.send(events::DeleteAllGuidesRequested{}); }});
            return items;
        }
    }

    // Inside a non-hidden plane's rectangle, nearest-t wins.
    {
        const Bbox box = modelBbox(ctx.model());
        const double half = sectionHalfExtent(box);
        geo::Id bestId = geo::kInvalidId;
        bool bestActive = false;
        double bestT = std::numeric_limits<double>::max();
        for (const tools::SectionPlaneData& plane : ctx.sections()) {
            if (plane.hidden) continue;  // a hidden plane's outline isn't clickable
            const double denom = geo::dot(e.ray.dir, plane.normal);
            if (std::fabs(denom) < geo::kEps) continue;  // ray parallel to this plane
            const double t = geo::dot(plane.point - e.ray.origin, plane.normal) / denom;
            if (t <= geo::kEps || t >= bestT) continue;  // behind the camera, or not nearer than the current best
            const geo::Vec3 hitPoint = e.ray.origin + e.ray.dir * t;
            const auto [u, v] = planeBasis(plane.normal);
            const double du = geo::dot(hitPoint - plane.point, u);
            const double dv = geo::dot(hitPoint - plane.point, v);
            if (std::fabs(du) > half || std::fabs(dv) > half) continue;  // outside this plane's own rectangle
            bestT = t;
            bestId = plane.id;
            bestActive = plane.active;
        }
        if (bestId != geo::kInvalidId) {
            const geo::Id planeId = bestId;
            items.push_back(ContextMenuItem{
                "Reverse", false, false,
                [&kernel, planeId]() { kernel.send(events::ReverseSectionRequested{planeId}); }});
            items.push_back(ContextMenuItem{
                "Active Cut", true, bestActive, [&kernel, planeId, bestActive]() {
                    kernel.send(events::SetSectionActiveRequested{planeId, !bestActive});
                }});
            // No "Align View" item; no disabled placeholder either.
            return items;
        }
    }

    // Origin point, or any of the 3 frame lines.
    {
        const tools::AxesFrame frame = ctx.axesFrame();
        bool hit = distancePointToRay(frame.origin, e.ray) <= e.tols.vertexTol;
        if (!hit) hit = closestPointOnLineToRay(frame.origin, frame.xDir, e.ray).distToRay <= e.tols.edgeTol;
        if (!hit) hit = closestPointOnLineToRay(frame.origin, frame.yDir, e.ray).distToRay <= e.tols.edgeTol;
        if (!hit) hit = closestPointOnLineToRay(frame.origin, frame.zDir, e.ray).distToRay <= e.tols.edgeTol;
        if (hit) {
            items.push_back(ContextMenuItem{
                "Reset", false, false, [&kernel]() { kernel.send(events::ResetAxesRequested{}); }});
            return items;
        }
    }

    // Face interior with a textured front material only; checked last
    // (broadest possible target).
    {
        const geo::PickResult faceHit = ctx.pick(e, geo::PickOptions{0.0, 0.0});
        if (faceHit.kind == geo::PickKind::Face) {
            const geo::Id faceId = faceHit.id;
            const geo::Id materialId = ctx.frontMaterialOf(events::EntityRef{geo::EntityKind::Face, faceId});
            if (ctx.materialTextureInfo(materialId).textured) {
                items.push_back(ContextMenuItem{
                    "Position Texture", false, false, [startPositionTexture, faceId]() {
                        if (startPositionTexture) startPositionTexture(faceId);
                    }});
                return items;
            }
        }
    }

    // Nothing hit: no menu (the reference modeler's paste/view menu is out of scope).
    return items;
}

}  // namespace plnr::ui
