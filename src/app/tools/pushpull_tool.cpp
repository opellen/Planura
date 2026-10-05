#include "pushpull_tool.h"

#include <cmath>

#include <Qt>

#include <geo/triangulate.h>

namespace plnr::tools {

namespace {

void pushVertex(std::vector<float>& verts, const geo::Vec3& v) {
    verts.push_back(static_cast<float>(v.x));
    verts.push_back(static_cast<float>(v.y));
    verts.push_back(static_cast<float>(v.z));
}

// Signed distance along the line (anchor, unit axis) of the point closest to ray: the pointer's extrusion distance.
// Falls back to lastDistance when axis and ray.dir are near-parallel, so the preview doesn't flicker.
double signedDistanceOnAxis(const geo::Ray& ray, const geo::Vec3& anchor, const geo::Vec3& axis,
                             double lastDistance) {
    const double b = geo::dot(axis, ray.dir);
    const double denom = 1.0 - b * b;
    if (std::fabs(denom) < geo::kEps) return lastDistance;

    const geo::Vec3 w0 = anchor - ray.origin;
    const double d = geo::dot(axis, w0);
    const double e = geo::dot(ray.dir, w0);
    return (b * e - d) / denom;
}

// Pointer travel (screen pixels) beyond which a release is a drag-commit, not the first click.
constexpr double kDragThresholdPx = 5.0;

}  // namespace

void PushPullTool::onActivate(ToolContext& ctx) {
    arm_.reset();
    lastDistance_ = 0.0;
    ctx.setHint("Select a face to push or pull.");
}

void PushPullTool::onDeactivate(ToolContext& ctx) {
    arm_.reset();
    ctx.setPreview({}, std::nullopt);
    ctx.setHoverFace({});  // no stale dotted face after a tool switch
    ctx.setExtrudeGhost(geo::kInvalidId, 0.0);  // nor a stale prism ghost
}

void PushPullTool::reset(ToolContext& ctx) {
    arm_.reset();
    lastDistance_ = 0.0;
    ctx.setPreview({}, std::nullopt);
    ctx.setHoverFace({});
    ctx.setExtrudeGhost(geo::kInvalidId, 0.0);  // closes the drag's live prism
    ctx.setHint("Select a face to push or pull.");
}

void PushPullTool::updateHoverFace(ToolContext& ctx, const PointerEvent& e) const {
    // Blue dotted pattern before the extrusion is armed; same pick as the arming click.
    const geo::PickResult hit = ctx.pick(e, geo::PickOptions{0.0, 0.0});
    const geo::Model* model = ctx.model();
    if (hit.kind != geo::PickKind::Face || !model) {
        ctx.setHoverFace({});
        return;
    }

    // geo::triangulate returns vertex-id triples; resolve to positions. A dangling id clears rather than highlighting a partial face.
    const std::vector<geo::Id> triIds = geo::triangulate(*model, hit.id);
    std::vector<float> tris;
    tris.reserve(triIds.size() * 3);
    for (geo::Id vId : triIds) {
        const geo::Vertex* v = model->vertex(vId);
        if (!v) {
            ctx.setHoverFace({});
            return;
        }
        pushVertex(tris, v->pos);
    }
    ctx.setHoverFace(std::move(tris));
}

void PushPullTool::onPointerMove(ToolContext& ctx, const PointerEvent& e) {
    if (!arm_) {
        updateHoverFace(ctx, e);
        return;
    }

    const double distance = signedDistanceOnAxis(e.ray, arm_->anchor, arm_->axis, lastDistance_);
    lastDistance_ = distance;

    // The in-progress prism renders as shaded faces (presenter builds side/cap triangles from faceId + distance); the line preview adds edges.
    ctx.setExtrudeGhost(arm_->faceId, distance);

    const std::size_t n = arm_->loopBase.size();
    std::vector<float> lineVerts;
    lineVerts.reserve(n * 12);
    for (std::size_t i = 0; i < n; ++i) {
        const geo::Vec3& base = arm_->loopBase[i];
        const geo::Vec3& baseNext = arm_->loopBase[(i + 1) % n];
        const geo::Vec3 top = base + arm_->axis * distance;
        const geo::Vec3 topNext = baseNext + arm_->axis * distance;

        // Cap outline segment.
        pushVertex(lineVerts, top);
        pushVertex(lineVerts, topNext);
        // Vertical segment from the original edge to the cap.
        pushVertex(lineVerts, base);
        pushVertex(lineVerts, top);
    }
    ctx.setPreview(std::move(lineVerts), std::nullopt);

    ctx.setHint("Click to set extrusion distance.");
}

void PushPullTool::onPointerDown(ToolContext& ctx, const PointerEvent& e) {
    if (!arm_) {
        const geo::PickResult hit = ctx.pick(e, geo::PickOptions{0.0, 0.0});
        if (hit.kind != geo::PickKind::Face) return;

        const geo::Model* model = ctx.model();
        if (!model) return;
        const geo::Face* face = model->face(hit.id);
        if (!face) return;

        const std::vector<geo::Id> loopIds = model->faceVertexLoop(hit.id);
        if (loopIds.empty()) return;

        std::vector<geo::Vec3> loopBase;
        loopBase.reserve(loopIds.size());
        for (geo::Id vId : loopIds) {
            const geo::Vertex* v = model->vertex(vId);
            if (!v) return;  // inconsistent model -- bail rather than arm a partial loop
            loopBase.push_back(v->pos);
        }

        arm_ = Arm{hit.id, hit.point, face->normal, loopBase, e.screen};
        lastDistance_ = 0.0;
        // the reference modeler drops the dotted hover pattern the moment the drag starts.
        ctx.setHoverFace({});
        ctx.setHint("Click to set extrusion distance.");
        return;
    }

    // Armed: this click commits the extrusion.
    const double distance = signedDistanceOnAxis(e.ray, arm_->anchor, arm_->axis, lastDistance_);
    if (std::fabs(distance) >= geo::kMergeTol) {
        ctx.requestExtrudeFace(arm_->faceId, distance);
    }
    reset(ctx);
}

void PushPullTool::onPointerUp(ToolContext& ctx, const PointerEvent& e) {
    // Press-drag-release: a release commits only past the travel threshold; below it is the first half of click-move-click.
    if (!arm_) return;
    const QPointF travel = e.screen - arm_->pressScreen;
    if (std::hypot(travel.x(), travel.y()) < kDragThresholdPx) return;

    const double distance = signedDistanceOnAxis(e.ray, arm_->anchor, arm_->axis, lastDistance_);
    if (std::fabs(distance) >= geo::kMergeTol) {
        ctx.requestExtrudeFace(arm_->faceId, distance);
    }
    // A dragged-then-released gesture always ENDS here (releasing at zero distance commits nothing); never half-armed.
    reset(ctx);
}

void PushPullTool::onVcbCommit(ToolContext& ctx, const VcbValue& value) {
    if (!arm_ || value.kind != VcbValue::Kind::Scalar) {
        ctx.setHint("Invalid entry.");
        return;
    }
    const double mag = std::fabs(value.a);
    if (mag <= geo::kMergeTol) {
        ctx.setHint("Invalid entry.");
        return;
    }
    const double sign = lastDistance_ < 0.0 ? -1.0 : 1.0;

    // Same ctx.requestExtrudeFace as the click commit; only the distance source differs.
    ctx.requestExtrudeFace(arm_->faceId, sign * mag);
    reset(ctx);
}

void PushPullTool::onKeyDown(ToolContext& ctx, int key, bool /*ctrl*/) {
    if (key != Qt::Key_Escape) return;
    reset(ctx);
}

}  // namespace plnr::tools
