#include "move_tool.h"

#include <algorithm>
#include <cmath>
#include <optional>

#include <Qt>

#include <geo/triangulate.h>

namespace plnr::tools {

namespace {

// World axis (+X/+Y/+Z) closest to delta (largest absolute component) --
// the reference modeler's shift-lock rule. Falls back to +X for a near-zero delta.
geo::Vec3 dominantAxis(const geo::Vec3& delta) {
    const double ax = std::fabs(delta.x);
    const double ay = std::fabs(delta.y);
    const double az = std::fabs(delta.z);
    if (ax >= ay && ax >= az) return geo::Vec3{1.0, 0.0, 0.0};
    if (ay >= az) return geo::Vec3{0.0, 1.0, 0.0};
    return geo::Vec3{0.0, 0.0, 1.0};
}

void pushVertex(std::vector<float>& verts, const geo::Vec3& v) {
    verts.push_back(static_cast<float>(v.x));
    verts.push_back(static_cast<float>(v.y));
    verts.push_back(static_cast<float>(v.z));
}

// Ray ∩ plane(point, normal); nullopt when parallel or behind the origin.
std::optional<geo::Vec3> rayPlaneIntersect(const geo::Ray& ray, const geo::Vec3& point, const geo::Vec3& normal) {
    const double denom = geo::dot(ray.dir, normal);
    if (std::fabs(denom) < geo::kEps) return std::nullopt;
    const double t = geo::dot(point - ray.origin, normal) / denom;
    if (t <= geo::kEps) return std::nullopt;
    return ray.origin + ray.dir * t;
}

// Parameter s of the closest point (anchor + s*axis) to the ray. Requires
// axis and ray.dir to be unit length.
double axisParameterForRay(const geo::Ray& ray, const geo::Vec3& anchor, const geo::Vec3& axis) {
    const double b = geo::dot(axis, ray.dir);
    const double denom = 1.0 - b * b;
    if (std::fabs(denom) < geo::kEps) return 0.0;
    const geo::Vec3 w0 = anchor - ray.origin;
    const double d = geo::dot(axis, w0);
    const double e = geo::dot(ray.dir, w0);
    return (b * e - d) / denom;
}

bool containsId(const std::vector<geo::Id>& ids, geo::Id id) {
    return std::find(ids.begin(), ids.end(), id) != ids.end();
}

// Screen-space direction gate for the automatic axis snap: gates on the
// drag-plane aim direction vs. each axis's projection into that plane.
constexpr double kMoveOnAxisMaxAngleDeg = 12.0;
const double kMinMoveAxisAlignment = std::cos(kMoveOnAxisMaxAngleDeg * 3.14159265358979323846 / 180.0);
// An axis sighted nearly down the view direction has no usable screen
// direction; its in-plane projection length is the discriminator instead.
const double kMinMoveAxisProjection = std::sin(kMoveOnAxisMaxAngleDeg * 3.14159265358979323846 / 180.0);

// Drag-vs-click screen-pixel discriminator for the press-drag-release gesture.
constexpr double kDragThresholdPx = 5.0;

}  // namespace

void MoveTool::onActivate(ToolContext& ctx) {
    lastCommit_.reset();
    copyMode_ = false;
    ctrlHeld_ = false;
    reset(ctx);
}

void MoveTool::onDeactivate(ToolContext& ctx) {
    grab_.reset();
    lastCommit_.reset();
    copyMode_ = false;
    ctrlHeld_ = false;
    ctx.setPreview({}, std::nullopt);
    // Clears the move ghost, hover face, and inference cue alongside the preview.
    ctx.setMoveGhost({}, {});
    ctx.setHoverFace({});
    ctx.setInferenceCue(std::nullopt);
}

void MoveTool::syncCtrl(bool ctrlNow) {
    if (ctrlNow && !ctrlHeld_) {
        copyMode_ = !copyMode_;
    }
    ctrlHeld_ = ctrlNow;
}

std::vector<events::EntityRef> MoveTool::copyRefs(ToolContext& ctx) const {
    const events::EntityRef grabbedRef{grab_->kind, grab_->id};
    const std::vector<events::EntityRef>& sel = ctx.selection();
    for (const events::EntityRef& ref : sel) {
        if (ref == grabbedRef) return sel;  // grabbed entity is part of the selection -- copy all of it
    }
    return {grabbedRef};  // not selected (or nothing is) -- copy just the grabbed entity
}

void MoveTool::reset(ToolContext& ctx) {
    grab_.reset();
    ctx.setPreview({}, std::nullopt);
    // Closes the drag -- no ghost/hover/cue survives past a commit or cancel.
    ctx.setMoveGhost({}, {});
    ctx.setHoverFace({});  // no stale dotted face
    ctx.setInferenceCue(std::nullopt);  // no stale axis-snap cue
    ctx.setHint("Select entities to move. | Ctrl = Toggle Copy.");
}

void MoveTool::updateHoverFace(ToolContext& ctx, const PointerEvent& e) const {
    const geo::PickResult hit = ctx.pick(e, e.tols);
    const geo::Model* model = ctx.model();
    if (hit.kind != geo::PickKind::Face || !model) {
        ctx.setHoverFace({});
        return;
    }
    const std::vector<geo::Id> triIds = geo::triangulate(*model, hit.id);
    std::vector<float> tris;
    tris.reserve(triIds.size() * 3);
    for (geo::Id vId : triIds) {
        const geo::Vertex* v = model->vertex(vId);
        if (!v) {
            ctx.setHoverFace({});  // dangling id mid-mutation -- clear, don't highlight a partial face
            return;
        }
        pushVertex(tris, v->pos);
    }
    ctx.setHoverFace(std::move(tris));
}

MoveTool::TargetResolution MoveTool::resolveTarget(ToolContext& ctx, const PointerEvent& e) const {
    const geo::Model* model = ctx.model();
    if (!model || !grab_) return {};

    const geo::Vec3& anchor = grab_->grabPoint;

    // Endpoint snap to OTHER geometry (self-snap would pin the drag). Also
    // carries geo::infer's automatic OnAxis rung, consumed below if Endpoint doesn't win.
    const geo::Inference snap =
        geo::infer(*model, e.ray, geo::InferenceContext{anchor, std::nullopt, e.tols});
    const bool endpointSnap =
        snap.kind == geo::InferenceKind::Endpoint && !containsId(grab_->vertexIds, snap.refId);
    if (endpointSnap && !e.shift) {
        return {snap.pos, std::nullopt};
    }

    if (!e.shift) {
        // Consumes geo::infer's automatic OnAxis rung (ground-level drags);
        // dir is already signed toward the drawn direction.
        if (snap.kind == geo::InferenceKind::OnAxis && snap.dir) {
            return {snap.pos, *snap.dir};
        }

        // Free drag: cursor ray onto the camera-facing plane through the
        // grab point, never ground (see class comment).
        const std::optional<geo::Vec3> free = rayPlaneIntersect(e.ray, anchor, grab_->dragPlaneNormal);
        if (!free) return {};

        const geo::Vec3 d = *free - anchor;
        const double dLen = geo::length(d);
        // Anti-spurious gate: no snap while the cursor still sits on the anchor.
        if (dLen > e.tols.edgeTol) {
            // Screen-space direction gate: each candidate axis is tested via
            // its in-plane projection. Best alignment wins among axes
            // passing the cone; one sighted down the view direction is skipped.
            const geo::Vec3& n = grab_->dragPlaneNormal;
            bool found = false;
            double bestCos = 0.0;
            geo::Vec3 bestAxisAbs;
            geo::Vec3 bestSignedAxis;
            for (const geo::Vec3& axisAbs :
                 {geo::Vec3{1.0, 0.0, 0.0}, geo::Vec3{0.0, 1.0, 0.0}, geo::Vec3{0.0, 0.0, 1.0}}) {
                const geo::Vec3 proj = axisAbs - n * geo::dot(axisAbs, n);
                const double projLen = geo::length(proj);
                if (projLen < kMinMoveAxisProjection) continue;
                const double cosAngle = std::fabs(geo::dot(d, proj)) / (dLen * projLen);
                if (cosAngle < kMinMoveAxisAlignment) continue;
                if (!found || cosAngle > bestCos) {
                    found = true;
                    bestCos = cosAngle;
                    bestAxisAbs = axisAbs;
                    // Signed toward the aim's side of the projection, display-only.
                    bestSignedAxis = geo::dot(d, proj) < 0.0 ? (geo::Vec3{} - axisAbs) : axisAbs;
                }
            }
            if (found) {
                // Position math stays on the unsigned bestAxisAbs --
                // axisParameterForRay is anti-symmetric, so the signed axis
                // would land on the wrong side. Signed axis is display-only.
                return {anchor + bestAxisAbs * axisParameterForRay(e.ray, anchor, bestAxisAbs),
                        bestSignedAxis};
            }
        }
        return {free, std::nullopt};
    }

    // Shift: lock to the dominant world axis of the current aim, reporting
    // the signed axis for cue/color display.
    const std::optional<geo::Vec3> free = rayPlaneIntersect(e.ray, anchor, grab_->dragPlaneNormal);
    const geo::Vec3 probe = endpointSnap ? snap.pos : (free ? *free : anchor);
    const geo::Vec3 d = probe - anchor;
    const geo::Vec3 axisAbs = dominantAxis(d);
    const geo::Vec3 axis = geo::dot(d, axisAbs) < 0.0 ? (geo::Vec3{} - axisAbs) : axisAbs;
    return {anchor + axisAbs * axisParameterForRay(e.ray, anchor, axisAbs), axis};
}

void MoveTool::onPointerMove(ToolContext& ctx, const PointerEvent& e) {
    syncCtrl(e.ctrl);
    if (!grab_) {
        // Idle Move shows the dotted hover pattern on the face a click would grab.
        updateHoverFace(ctx, e);
        return;
    }

    const TargetResolution resolved = resolveTarget(ctx, e);
    if (!resolved.pos) {
        // Ray misses the drag plane: clear all channels rather than leaving
        // the previous frame's stale outline/ghost/stipple/cue on screen.
        ctx.setPreviewBatches({}, std::nullopt);
        ctx.setMoveGhost({}, {});
        ctx.setHoverFace({});
        ctx.setInferenceCue(std::nullopt);
        ctx.setHint("Click to place. | Ctrl = Toggle Copy.");
        return;
    }
    const geo::Vec3 target = *resolved.pos;

    const geo::Vec3 delta = target - grab_->grabPoint;

    // lineVerts/marker are computed the same regardless of copy mode; only
    // what's done with lineVerts differs (outline preview vs. move ghost).
    std::vector<float> lineVerts;
    std::optional<geo::Vec3> marker;

    switch (grab_->kind) {
        case geo::EntityKind::Vertex:
            marker = grab_->basePositions.empty() ? target : grab_->basePositions[0] + delta;
            break;
        case geo::EntityKind::Edge:
            if (grab_->basePositions.size() == 2) {
                pushVertex(lineVerts, grab_->basePositions[0] + delta);
                pushVertex(lineVerts, grab_->basePositions[1] + delta);
            }
            marker = target;
            break;
        case geo::EntityKind::Face: {
            const std::size_t n = grab_->basePositions.size();
            for (std::size_t i = 0; i < n; ++i) {
                const geo::Vec3 a = grab_->basePositions[i] + delta;
                const geo::Vec3 b = grab_->basePositions[(i + 1) % n] + delta;
                pushVertex(lineVerts, a);
                pushVertex(lineVerts, b);
            }
            marker = target;
            break;
        }
    }

    // Axis-colored move line + a markerless "On <Axis>" cue, covering both
    // the automatic snap and Shift's lock. No axis -> no cue.
    std::optional<ToolContext::PreviewBatch> axisBatch;
    if (resolved.axis) {
        std::vector<float> axisVerts;
        pushVertex(axisVerts, grab_->grabPoint);
        pushVertex(axisVerts, target);
        const PreviewColor axisColor = axisColorFor(*resolved.axis);
        axisBatch = ToolContext::PreviewBatch{std::move(axisVerts), axisColor.r, axisColor.g, axisColor.b, axisColor.a};

        InferenceCue cue;
        cue.pos = target;
        cue.shape = kMarkerNone;
        cue.screenTip = std::string("On ") + axisTipSuffixFor(*resolved.axis);
        ctx.setInferenceCue(cue);
    } else {
        ctx.setInferenceCue(std::nullopt);
    }

    std::vector<ToolContext::PreviewBatch> batches;
    if (copyMode_) {
        // Copy mode keeps an outline preview (the original stays put too) as a
        // default-colored batch alongside the axis batch, if any.
        batches.push_back(ToolContext::PreviewBatch{
            std::move(lineVerts), kDefaultPreviewColor.r, kDefaultPreviewColor.g, kDefaultPreviewColor.b,
            kDefaultPreviewColor.a});
    }
    if (axisBatch) batches.push_back(std::move(*axisBatch));

    ctx.setPreviewBatches(std::move(batches), marker);
    if (copyMode_) {
        // Copy mode sends no ghost (translating originals would be wrong);
        // explicitly clears so a pre-toggle ghost doesn't linger.
        ctx.setMoveGhost({}, {});
    } else {
        // Geometry itself follows the cursor here (ViewportPresenter re-renders
        // grab_->vertexIds by delta), so only the marker/axis batch survive.
        ctx.setMoveGhost(grab_->vertexIds, delta);
    }

    // Dotted pattern rides the grabbed face: displaced for a non-copy drag,
    // at its original position in copy mode. Non-face grabs clear it.
    if (grab_->kind == geo::EntityKind::Face) {
        const geo::Model* model = ctx.model();
        const geo::Vec3 offset = copyMode_ ? geo::Vec3{} : delta;
        std::vector<float> hoverTris;
        if (model) {
            const std::vector<geo::Id> triIds = geo::triangulate(*model, grab_->id);
            hoverTris.reserve(triIds.size() * 3);
            for (geo::Id vId : triIds) {
                const geo::Vertex* v = model->vertex(vId);
                if (!v) {
                    hoverTris.clear();  // dangling id -- no partial-face highlight
                    break;
                }
                pushVertex(hoverTris, v->pos + offset);
            }
        }
        ctx.setHoverFace(std::move(hoverTris));
    } else {
        ctx.setHoverFace({});
    }

    ctx.setHint("Click to place. | Ctrl = Toggle Copy.");
}

void MoveTool::onPointerDown(ToolContext& ctx, const PointerEvent& e) {
    syncCtrl(e.ctrl);

    if (!grab_) {
        const geo::PickResult hit = ctx.pick(e, e.tols);
        if (hit.kind == geo::PickKind::None) return;

        const geo::Model* model = ctx.model();
        if (!model) return;

        geo::EntityKind kind{};
        switch (hit.kind) {
            case geo::PickKind::Vertex:
                kind = geo::EntityKind::Vertex;
                break;
            case geo::PickKind::Edge:
                kind = geo::EntityKind::Edge;
                break;
            case geo::PickKind::Face:
                kind = geo::EntityKind::Face;
                break;
            case geo::PickKind::None:
                return;
        }

        const std::vector<geo::Id> vertexIds = geo::collectVertices(*model, kind, hit.id);
        if (vertexIds.empty()) return;

        std::vector<geo::Vec3> basePositions;
        basePositions.reserve(vertexIds.size());
        for (geo::Id vId : vertexIds) {
            const geo::Vertex* v = model->vertex(vId);
            if (!v) return;  // inconsistent model -- bail rather than grab a partial set
            basePositions.push_back(v->pos);
        }

        // Drag plane normal: camera-facing at grab time, fixed for the whole drag.
        const geo::Vec3 planeNormal = geo::Vec3{} - e.ray.dir;
        grab_ = Grab{kind, hit.id, hit.point, vertexIds, basePositions, planeNormal, e.screen};
        ctx.setHint("Click to place. | Ctrl = Toggle Copy.");
        return;
    }

    // Grabbed: this click commits the move.
    commitDrag(ctx, e);
}

void MoveTool::commitDrag(ToolContext& ctx, const PointerEvent& e) {
    // Non-copy: requestMoveEntity in place. Copy: requestTransformEntities
    // at copies=1 over copyRefs().
    const TargetResolution resolved = resolveTarget(ctx, e);
    if (resolved.pos) {
        const geo::Vec3 delta = *resolved.pos - grab_->grabPoint;
        if (geo::length(delta) >= geo::kMergeTol) {
            if (copyMode_) {
                events::TransformSpec spec;
                spec.kind = events::TransformSpec::Kind::Translation;
                spec.delta = delta;
                ctx.requestTransformEntities(copyRefs(ctx), spec, /*copies=*/1);
            } else {
                ctx.requestMoveEntity(grab_->kind, grab_->id, delta);
            }
            // Records lastCommit_ only on an actual request; a near-zero
            // delta leaves any open window untouched.
            lastCommit_ = LastCommit{grab_->kind, grab_->id, delta, copyMode_};
        }
    }
    reset(ctx);
}

void MoveTool::onPointerUp(ToolContext& ctx, const PointerEvent& e) {
    // Same commit as the second click, once travel passes the drag
    // threshold; sub-threshold keeps the grab waiting (click-move-click).
    if (!grab_) return;
    const QPointF travel = e.screen - grab_->pressScreen;
    if (std::hypot(travel.x(), travel.y()) < kDragThresholdPx) return;
    syncCtrl(e.ctrl);
    commitDrag(ctx, e);
}

void MoveTool::onKeyDown(ToolContext& ctx, int key, bool /*ctrl*/) {
    if (key != Qt::Key_Escape) return;
    // Escape resets copy mode/retro-edit window and re-zeroes ctrlHeld_ so
    // a still-held Ctrl re-syncs as a rising edge on the next pointer-move.
    lastCommit_.reset();
    copyMode_ = false;
    ctrlHeld_ = false;
    reset(ctx);
}

void MoveTool::onVcbCommit(ToolContext& ctx, const VcbValue& value) {
    if (grab_) {
        // No VCB form for the in-progress drag itself -- only the post-commit
        // retro-edit window is wired. Ignore silently.
        return;
    }

    if (!lastCommit_) {
        ctx.setHint("Invalid distance entered.");
        return;
    }

    if (lastCommit_->wasCopy) {
        // Arrays are valid only right after a copy commit -- route straight
        // to GeometryApi's own array retro-edit window.
        switch (value.kind) {
            case VcbValue::Kind::ArrayTimes:
                if (value.count < 1) {
                    ctx.setHint("Invalid distance entered.");
                    return;
                }
                ctx.requestApplyArrayTimes(value.count);
                return;
            case VcbValue::Kind::ArrayDivide:
                if (value.count < 1) {
                    ctx.setHint("Invalid distance entered.");
                    return;
                }
                ctx.requestApplyArrayDivide(value.count);
                return;
            default:
                ctx.setHint("Invalid distance entered.");
                return;
        }
    }

    // Non-copy retype: requestMoveEntity only ever applies a delta (no
    // "un-move"), so a Scalar retype sends the DIFFERENCE between the new
    // and already-applied distance -- lands at the new absolute distance.
    if (value.kind != VcbValue::Kind::Scalar) {
        ctx.setHint("Invalid distance entered.");
        return;
    }
    const geo::Vec3 dir = geo::normalized(lastCommit_->delta);
    const geo::Vec3 newDelta = dir * value.a;
    const geo::Vec3 deltaDiff = newDelta - lastCommit_->delta;
    if (geo::length(deltaDiff) >= geo::kMergeTol) {
        ctx.requestMoveEntity(lastCommit_->kind, lastCommit_->id, deltaDiff);
    }
    lastCommit_->delta = newDelta;
}

}  // namespace plnr::tools
