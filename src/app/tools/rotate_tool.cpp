#include "rotate_tool.h"

#include <cmath>
#include <iomanip>
#include <sstream>
#include <string>
#include <utility>

#include <Qt>

#include <geo/model.h>
#include <geo/shapes.h>

namespace plnr::tools {

namespace {

// "~ " + value formatted to 2 decimals -- the VCB's approximate-readout convention.
std::string formatApprox(double value) {
    std::ostringstream oss;
    oss << "~ " << std::fixed << std::setprecision(2) << value;
    return oss.str();
}

inline constexpr double kPi = 3.14159265358979323846;
inline constexpr double kRadToDeg = 180.0 / kPi;
inline constexpr double kDegToRad = kPi / 180.0;

// Ray ∩ plane(point, normal); nullopt when parallel or behind the ray origin.
std::optional<geo::Vec3> rayPlaneIntersect(const geo::Ray& ray, const geo::Vec3& point, const geo::Vec3& normal) {
    const double denom = geo::dot(ray.dir, normal);
    if (std::fabs(denom) < geo::kEps) return std::nullopt;
    const double t = geo::dot(point - ray.origin, normal) / denom;
    if (t <= geo::kEps) return std::nullopt;
    return ray.origin + ray.dir * t;
}

// Signed angle in (-pi, pi] from `from` to `to` about unit axis `normal`;
// no continuity tracking across the wrap.
double signedAngle(const geo::Vec3& from, const geo::Vec3& to, const geo::Vec3& normal) {
    const double sinPart = geo::dot(normal, geo::cross(from, to));
    const double cosPart = geo::dot(from, to);
    return std::atan2(sinPart, cosPart);
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

// Turns a closed point loop into consecutive-pair + closing-pair line segments.
void appendLoop(std::vector<float>& verts, const std::vector<geo::Vec3>& points) {
    if (points.empty()) return;
    verts.reserve(verts.size() + points.size() * 6);
    for (std::size_t i = 0; i < points.size(); ++i) {
        appendSegment(verts, points[i], points[(i + 1) % points.size()]);
    }
}

}  // namespace

void RotateTool::onActivate(ToolContext& ctx) {
    lastCommit_.reset();
    copyMode_ = false;
    ctrlHeld_ = false;
    reset(ctx);
}

void RotateTool::onDeactivate(ToolContext& ctx) {
    stage_ = Stage::PlaneDetect;
    lastCursor_.reset();
    shiftLock_.reset();
    lastCommit_.reset();
    ctx.setPreviewBatches({}, std::nullopt);
}

void RotateTool::syncCtrl(bool ctrlNow) {
    if (ctrlNow && !ctrlHeld_) {
        copyMode_ = !copyMode_;
    }
    ctrlHeld_ = ctrlNow;
}

RotateTool::PlaneHit RotateTool::resolvePlaneHit(ToolContext& ctx, const PointerEvent& e) const {
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
        // Vertex/Edge hit: keep the hit point, fall back to the ground-plane normal.
        return PlaneHit{hit.point, geo::Vec3{0.0, 0.0, 1.0}, true};
    }

    // Nothing picked: fall back to the ray's own ground-plane (z=0) intersection.
    const std::optional<geo::Vec3> ground = rayPlaneIntersect(e.ray, geo::Vec3{0.0, 0.0, 0.0}, geo::Vec3{0.0, 0.0, 1.0});
    if (!ground) return PlaneHit{geo::Vec3{}, geo::Vec3{0.0, 0.0, 1.0}, false};
    return PlaneHit{*ground, geo::Vec3{0.0, 0.0, 1.0}, true};
}

std::optional<geo::Vec3> RotateTool::resolveOnFixedPlane(const PointerEvent& e) const {
    return rayPlaneIntersect(e.ray, pivot_, planeNormal_);
}

double RotateTool::liveAngle(const geo::Vec3& cursor) const {
    const geo::Vec3 dirTo = cursor - pivot_;
    if (geo::length(dirTo) <= geo::kMergeTol) return 0.0;
    return signedAngle(rayDir_, dirTo, planeNormal_);
}

double RotateTool::protractorRadius(const PointerEvent& e) const {
    if (stage_ == Stage::RaySet) {
        const double d = geo::length(rayDir_);
        if (d > geo::kMergeTol) return d;
    }
    // Tolerance-scaled default -- keeps the protractor a roughly constant
    // on-screen size regardless of zoom.
    return e.tols.vertexTol * 10.0;
}

void RotateTool::appendSelectionRotated(ToolContext& ctx, std::vector<float>& verts, double angleRad) const {
    const geo::Model* model = ctx.model();
    if (!model) return;
    const std::vector<events::EntityRef>& sel = ctx.selection();
    if (sel.empty()) return;

    std::vector<std::pair<geo::EntityKind, geo::Id>> seeds;
    seeds.reserve(sel.size());
    for (const events::EntityRef& ref : sel) seeds.emplace_back(ref.kind, ref.id);
    const geo::EntitySet closure = geo::closureOf(*model, seeds);

    const geo::Transform xf = geo::Transform::rotation(pivot_, planeNormal_, angleRad);
    for (geo::Id edgeId : closure.edges) {
        const geo::Edge* edge = model->edge(edgeId);
        if (!edge) continue;
        const geo::HalfEdge* h0 = model->halfEdge(edge->halfEdges[0]);
        const geo::HalfEdge* h1 = model->halfEdge(edge->halfEdges[1]);
        if (!h0 || !h1) continue;
        const geo::Vertex* v0 = model->vertex(h0->origin);
        const geo::Vertex* v1 = model->vertex(h1->origin);
        if (!v0 || !v1) continue;
        appendSegment(verts, xf.apply(v0->pos), xf.apply(v1->pos));
    }
}

PreviewColor RotateTool::classifyPlaneColor() const {
    // Tints the protractor to whichever world axis the plane normal is
    // aligned with, neutral otherwise.
    constexpr double kAxisAlignThreshold = 0.999;
    if (std::fabs(geo::dot(planeNormal_, geo::Vec3{1.0, 0.0, 0.0})) > kAxisAlignThreshold) return kAxisRedColor;
    if (std::fabs(geo::dot(planeNormal_, geo::Vec3{0.0, 1.0, 0.0})) > kAxisAlignThreshold) return kAxisGreenColor;
    if (std::fabs(geo::dot(planeNormal_, geo::Vec3{0.0, 0.0, 1.0})) > kAxisAlignThreshold) return kAxisBlueColor;
    return kDefaultPreviewColor;
}

std::vector<ToolContext::PreviewBatch> RotateTool::buildPreview(ToolContext& ctx, double angleRad, double radius) const {
    std::vector<ToolContext::PreviewBatch> batches;

    // Circle vertex 0 sits along the first ray once set, else an arbitrary
    // in-plane direction; colored via classifyPlaneColor (the only piece
    // that ever draws in a non-default color).
    std::vector<float> circleVerts;
    const geo::Vec3 startDir = (stage_ == Stage::RaySet) ? rayDir_ : geo::Vec3{1.0, 0.0, 0.0};
    appendLoop(circleVerts, geo::regularPolygonPoints(pivot_, radius, planeNormal_, kCircleSegments, startDir,
                                                       /*circumscribed=*/false));
    const PreviewColor axisColor = classifyPlaneColor();
    batches.push_back(ToolContext::PreviewBatch{std::move(circleVerts), axisColor.r, axisColor.g, axisColor.b, axisColor.a});

    if (stage_ == Stage::RaySet) {
        std::vector<float> rayVerts;
        appendSegment(rayVerts, pivot_, pivot_ + rayDir_);
        // Rotates the FIRST ray by angleRad (rather than drawing to the raw
        // cursor) so the tick stays exactly radius from the pivot.
        const geo::Transform xf = geo::Transform::rotation(pivot_, planeNormal_, angleRad);
        appendSegment(rayVerts, pivot_, xf.apply(pivot_ + rayDir_));
        batches.push_back(ToolContext::PreviewBatch{std::move(rayVerts), kDefaultPreviewColor.r, kDefaultPreviewColor.g,
                                                      kDefaultPreviewColor.b, kDefaultPreviewColor.a});
    }

    std::vector<float> selectionVerts;
    appendSelectionRotated(ctx, selectionVerts, angleRad);
    if (!selectionVerts.empty()) {
        batches.push_back(ToolContext::PreviewBatch{std::move(selectionVerts), kDefaultPreviewColor.r,
                                                      kDefaultPreviewColor.g, kDefaultPreviewColor.b, kDefaultPreviewColor.a});
    }

    return batches;
}

void RotateTool::commit(ToolContext& ctx, double angleRad) {
    const std::vector<events::EntityRef>& sel = ctx.selection();
    if (sel.empty()) {
        // Empty selection is a hint only -- pivot/ray clicks never grab geometry.
        ctx.setHint("Select something first.");
        reset(ctx);
        return;
    }

    events::TransformSpec spec;
    spec.kind = events::TransformSpec::Kind::Rotation;
    spec.point = pivot_;
    spec.axis = planeNormal_;
    spec.angleRad = angleRad;

    const int copies = copyMode_ ? 1 : 0;
    ctx.requestTransformEntities(sel, spec, copies);

    lastCommit_ = LastCommit{sel, pivot_, planeNormal_, angleRad, copyMode_};
    reset(ctx);
}

void RotateTool::reset(ToolContext& ctx) {
    stage_ = Stage::PlaneDetect;
    lastCursor_.reset();
    shiftLock_.reset();
    ctx.setPreviewBatches({}, std::nullopt);
    ctx.setHint("Click to set the rotation or enter angle. | Ctrl = Toggle Copy.");
    updateVcb(ctx);
}

void RotateTool::updateVcb(ToolContext& ctx) const {
    // Rotate's VCB is always "Angle" -- no earlier stage with a different meaning.
    ctx.setVcbLabel("Angle");
    if (stage_ == Stage::RaySet && lastCursor_) {
        ctx.setVcbValue(formatApprox(liveAngle(*lastCursor_) * kRadToDeg));
    }
}

void RotateTool::onPointerMove(ToolContext& ctx, const PointerEvent& e) {
    syncCtrl(e.ctrl);

    switch (stage_) {
        case Stage::PlaneDetect: {
            PlaneHit hit;
            if (e.shift) {
                if (!shiftLock_) shiftLock_ = resolvePlaneHit(ctx, e);
                if (shiftLock_->valid) {
                    const std::optional<geo::Vec3> onLocked =
                        rayPlaneIntersect(e.ray, shiftLock_->point, shiftLock_->normal);
                    hit = PlaneHit{onLocked.value_or(shiftLock_->point), shiftLock_->normal, true};
                } else {
                    hit = *shiftLock_;
                }
            } else {
                shiftLock_.reset();
                hit = resolvePlaneHit(ctx, e);
            }

            if (hit.valid) {
                pivot_ = hit.point;
                planeNormal_ = hit.normal;
                lastCursor_ = hit.point;
            }

            std::optional<geo::Vec3> marker;
            if (hit.valid) marker = hit.point;
            ctx.setPreviewBatches(buildPreview(ctx, 0.0, protractorRadius(e)), marker);
            ctx.setHint("Click to set the rotation or enter angle. | Ctrl = Toggle Copy.");
            updateVcb(ctx);
            return;
        }
        case Stage::PivotSet: {
            const std::optional<geo::Vec3> onPlane = resolveOnFixedPlane(e);
            if (onPlane) lastCursor_ = onPlane;
            ctx.setPreviewBatches(buildPreview(ctx, 0.0, protractorRadius(e)), lastCursor_);
            ctx.setHint("Click to set the rotation or enter angle. | Ctrl = Toggle Copy.");
            updateVcb(ctx);
            return;
        }
        case Stage::RaySet: {
            const std::optional<geo::Vec3> onPlane = resolveOnFixedPlane(e);
            double angle = 0.0;
            if (onPlane) {
                lastCursor_ = onPlane;
                angle = liveAngle(*onPlane);
            }
            ctx.setPreviewBatches(buildPreview(ctx, angle, protractorRadius(e)), lastCursor_);
            ctx.setHint("Click to set the angle, or enter an angle. | Ctrl = Toggle Copy.");
            updateVcb(ctx);
            return;
        }
    }
}

void RotateTool::onPointerDown(ToolContext& ctx, const PointerEvent& e) {
    syncCtrl(e.ctrl);

    switch (stage_) {
        case Stage::PlaneDetect: {
            PlaneHit hit;
            if (e.shift && shiftLock_ && shiftLock_->valid) {
                const std::optional<geo::Vec3> onLocked =
                    rayPlaneIntersect(e.ray, shiftLock_->point, shiftLock_->normal);
                hit = PlaneHit{onLocked.value_or(shiftLock_->point), shiftLock_->normal, true};
            } else {
                hit = resolvePlaneHit(ctx, e);
            }
            if (!hit.valid) return;

            pivot_ = hit.point;
            planeNormal_ = hit.normal;
            // Starting a fresh rotation invalidates any prior retro-edit
            // window -- a typed follow-up must apply to THIS rotation.
            lastCommit_.reset();
            stage_ = Stage::PivotSet;
            ctx.setHint("Click to set the rotation or enter angle. | Ctrl = Toggle Copy.");
            updateVcb(ctx);
            return;
        }
        case Stage::PivotSet: {
            const std::optional<geo::Vec3> onPlane = resolveOnFixedPlane(e);
            if (!onPlane) return;
            const geo::Vec3 dir = *onPlane - pivot_;
            if (geo::length(dir) <= geo::kMergeTol) {
                // Degenerate ray (cursor back on the pivot) -- ignore, stay armed.
                return;
            }
            rayDir_ = dir;
            lastCursor_ = *onPlane;
            stage_ = Stage::RaySet;
            ctx.setHint("Click to set the angle, or enter an angle. | Ctrl = Toggle Copy.");
            updateVcb(ctx);
            return;
        }
        case Stage::RaySet: {
            const std::optional<geo::Vec3> onPlane = resolveOnFixedPlane(e);
            if (!onPlane) return;  // ray parallel to the protractor plane -- nothing to commit at
            commit(ctx, liveAngle(*onPlane));
            return;
        }
    }
}

void RotateTool::onKeyDown(ToolContext& ctx, int key, bool /*ctrl*/) {
    if (key != Qt::Key_Escape) return;
    lastCommit_.reset();
    reset(ctx);
}

void RotateTool::onVcbCommit(ToolContext& ctx, const VcbValue& value) {
    switch (stage_) {
        case Stage::PlaneDetect: {
            if (!lastCommit_) {
                ctx.setHint("Invalid angle entered.");
                return;
            }

            if (lastCommit_->wasCopy) {
                // Arrays are valid only right after a copy commit -- route
                // straight to GeometryApi's own array retro-edit window.
                switch (value.kind) {
                    case VcbValue::Kind::ArrayTimes:
                        if (value.count < 1) {
                            ctx.setHint("Invalid angle entered.");
                            return;
                        }
                        ctx.requestApplyArrayTimes(value.count);
                        return;
                    case VcbValue::Kind::ArrayDivide:
                        if (value.count < 1) {
                            ctx.setHint("Invalid angle entered.");
                            return;
                        }
                        ctx.requestApplyArrayDivide(value.count);
                        return;
                    default:
                        ctx.setHint("Invalid angle entered.");
                        return;
                }
            }

            // Non-copy retype: requestTransformEntities only applies forward
            // (no undo snapshot), so a Scalar retype sends the DELTA between
            // the new and previous angle -- lands at the new absolute angle.
            if (value.kind != VcbValue::Kind::Scalar) {
                ctx.setHint("Invalid angle entered.");
                return;
            }
            const double newAngleRad = value.a * kDegToRad;
            const double deltaRad = newAngleRad - lastCommit_->angleRad;

            events::TransformSpec spec;
            spec.kind = events::TransformSpec::Kind::Rotation;
            spec.point = lastCommit_->pivot;
            spec.axis = lastCommit_->normal;
            spec.angleRad = deltaRad;
            ctx.requestTransformEntities(lastCommit_->refs, spec, /*copies=*/0);
            lastCommit_->angleRad = newAngleRad;
            return;
        }
        case Stage::PivotSet:
            // No VCB form defined here -- no reference ray to measure against yet.
            ctx.setHint("Invalid angle entered.");
            return;
        case Stage::RaySet: {
            if (value.kind != VcbValue::Kind::Scalar) {
                ctx.setHint("Invalid angle entered.");
                return;
            }
            // Sign from the live cursor's side (if known), magnitude from the typed value.
            double angleRad = value.a * kDegToRad;
            if (lastCursor_) {
                const double liveRad = liveAngle(*lastCursor_);
                const double sign = liveRad < 0.0 ? -1.0 : 1.0;
                angleRad = sign * std::fabs(angleRad);
            }
            commit(ctx, angleRad);
            return;
        }
    }
}

}  // namespace plnr::tools
