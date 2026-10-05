#include "protractor_tool.h"

#include <algorithm>
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

inline constexpr double kPi = 3.14159265358979323846;
inline constexpr double kRadToDeg = 180.0 / kPi;
inline constexpr double kDegToRad = kPi / 180.0;

// Every committed/displayed angle rounds to this (0.1-degree precision cap).
constexpr double kPrecisionDeg = 0.1;
// Tick spacing: 15 degrees apart.
constexpr double kTickDeg = 15.0;
constexpr int kTickCount = 24;  // 360 / kTickDeg

// Cursor within kSnapRadiusMultiplier * radius of the pivot snaps to the
// nearest 15-degree tick; farther out reads a free, 0.1-degree-capped angle.
constexpr double kSnapRadiusMultiplier = 2.0;

// "~ " + value formatted to 2 decimals -- the VCB's approximate-readout convention.
std::string formatApprox(double value) {
    std::ostringstream oss;
    oss << "~ " << std::fixed << std::setprecision(2) << value;
    return oss.str();
}

// Same formatting, no "~ " prefix -- for the frozen (measure-only) exact readout.
std::string formatExact(double value) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2) << value;
    return oss.str();
}

// Ray ∩ plane(point, normal); nullopt when parallel or behind the ray origin.
std::optional<geo::Vec3> rayPlaneIntersect(const geo::Ray& ray, const geo::Vec3& point, const geo::Vec3& normal) {
    const double denom = geo::dot(ray.dir, normal);
    if (std::fabs(denom) < geo::kEps) return std::nullopt;
    const double t = geo::dot(point - ray.origin, normal) / denom;
    if (t <= geo::kEps) return std::nullopt;
    return ray.origin + ray.dir * t;
}

// Signed angle (radians, in (-pi, pi]) from `from` to `to` about unit axis
// `normal` (no continuity tracking across multiple calls).
double signedAngle(const geo::Vec3& from, const geo::Vec3& to, const geo::Vec3& normal) {
    const double sinPart = geo::dot(normal, geo::cross(from, to));
    const double cosPart = geo::dot(from, to);
    return std::atan2(sinPart, cosPart);
}

double roundTo(double value, double step) {
    return std::round(value / step) * step;
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

void appendLoop(std::vector<float>& verts, const std::vector<geo::Vec3>& points) {
    if (points.empty()) return;
    verts.reserve(verts.size() + points.size() * 6);
    for (std::size_t i = 0; i < points.size(); ++i) {
        appendSegment(verts, points[i], points[(i + 1) % points.size()]);
    }
}

// Arbitrary deterministic unit vector perpendicular to n (assumed unit,
// nonzero); duplicated from geo::shapes.cpp so bases stay aligned.
geo::Vec3 arbitraryPerpendicular(const geo::Vec3& n) {
    const geo::Vec3 axis = std::fabs(n.x) < 0.9 ? geo::Vec3{1.0, 0.0, 0.0} : geo::Vec3{0.0, 1.0, 0.0};
    return geo::normalized(geo::cross(n, axis));
}

// Same (u, v) plane basis regularPolygonPoints derives, so a tick sits at
// exactly the same angle convention as the circle's own vertices.
std::pair<geo::Vec3, geo::Vec3> tickBasis(const geo::Vec3& normal, const geo::Vec3& startDir) {
    const geo::Vec3 n = geo::normalized(normal);
    const geo::Vec3 proj = startDir - n * geo::dot(startDir, n);
    const double projLen = geo::length(proj);
    const geo::Vec3 u = projLen < geo::kEps ? arbitraryPerpendicular(n) : proj * (1.0 / projLen);
    const geo::Vec3 v = geo::normalized(geo::cross(n, u));
    return {u, v};
}

// A guide-line preview extends a fixed, finite span each way from its
// through-point; the dash pattern is baked into vertex data.
constexpr double kPreviewGuideExtent = 1000.0;
constexpr double kPreviewDashLen = 0.5;
constexpr double kPreviewGapLen = 0.5;

void appendDashedSegment(std::vector<float>& out, const geo::Vec3& from, const geo::Vec3& to) {
    const geo::Vec3 dir = to - from;
    const double totalLen = geo::length(dir);
    if (totalLen < geo::kEps) return;
    const geo::Vec3 unit = dir * (1.0 / totalLen);
    for (double t = 0.0; t < totalLen; t += kPreviewDashLen + kPreviewGapLen) {
        const double segEnd = std::min(t + kPreviewDashLen, totalLen);
        appendVertex(out, from + unit * t);
        appendVertex(out, from + unit * segEnd);
    }
}

}  // namespace

void ProtractorTool::onActivate(ToolContext& ctx) {
    guideMode_ = true;  // documented default; Ctrl toggles
    ctrlHeld_ = false;
    lockedArrowKey_ = 0;
    reset(ctx);
}

void ProtractorTool::onDeactivate(ToolContext& ctx) {
    stage_ = Stage::PlaneDetect;
    lastCursor_.reset();
    shiftLock_.reset();
    altFreeze_.reset();
    frozen_ = false;
    ctx.setPreviewBatches({}, std::nullopt);
    ctx.setInferenceCue(std::nullopt);
}

void ProtractorTool::syncCtrl(bool ctrlNow) {
    if (ctrlNow && !ctrlHeld_) guideMode_ = !guideMode_;
    ctrlHeld_ = ctrlNow;
}

ProtractorTool::PlaneHit ProtractorTool::resolvePlaneHit(ToolContext& ctx, const PointerEvent& e) const {
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

ProtractorTool::PlaneHit ProtractorTool::currentPlaneCandidate(ToolContext& ctx, const PointerEvent& e) {
    // Precedence: arrow lock > Alt freeze > Shift lock > plain hover.
    if (lockedArrowKey_ != 0) {
        const std::optional<geo::Vec3> onLocked = rayPlaneIntersect(e.ray, arrowLockAnchor_, arrowLockAxis_);
        return PlaneHit{onLocked.value_or(arrowLockAnchor_), arrowLockAxis_, true};
    }

    if (e.alt) {
        if (!altFreeze_) altFreeze_ = resolvePlaneHit(ctx, e);
        return *altFreeze_;  // fully frozen -- no sliding, no re-detection while held
    }
    altFreeze_.reset();

    if (e.shift) {
        if (!shiftLock_) shiftLock_ = resolvePlaneHit(ctx, e);
        if (shiftLock_->valid) {
            const std::optional<geo::Vec3> onLocked = rayPlaneIntersect(e.ray, shiftLock_->point, shiftLock_->normal);
            return PlaneHit{onLocked.value_or(shiftLock_->point), shiftLock_->normal, true};
        }
        return *shiftLock_;
    }
    shiftLock_.reset();

    return resolvePlaneHit(ctx, e);
}

std::optional<geo::Vec3> ProtractorTool::resolveOnFixedPlane(const PointerEvent& e) const {
    return rayPlaneIntersect(e.ray, pivot_, planeNormal_);
}

ProtractorTool::AngleReading ProtractorTool::computeAngle(const geo::Vec3& cursor, double radius) const {
    const geo::Vec3 dirTo = cursor - pivot_;
    double rad = 0.0;
    if (geo::length(dirTo) > geo::kMergeTol) rad = signedAngle(rayDir_, dirTo, planeNormal_);
    double deg = rad * kRadToDeg;

    const double dist = geo::length(dirTo);
    const bool nearCenter = radius > geo::kMergeTol && dist <= radius * kSnapRadiusMultiplier;
    bool snapped = false;
    if (nearCenter) {
        deg = roundTo(deg, kTickDeg);
        snapped = true;
    }
    deg = roundTo(deg, kPrecisionDeg);  // 0.1-degree precision cap, applied regardless of the tick snap above
    return AngleReading{deg, deg * kDegToRad, snapped};
}

double ProtractorTool::protractorRadius(const PointerEvent& e) const {
    if (stage_ == Stage::BaselineSet) return committedRadius();
    // Tolerance-scaled default -- keeps the protractor a roughly constant
    // on-screen size regardless of zoom.
    return e.tols.vertexTol * 10.0;
}

double ProtractorTool::committedRadius() const {
    const double d = geo::length(rayDir_);
    return d > geo::kMergeTol ? d : 1.0;
}

geo::Vec3 ProtractorTool::sweepTip(double angleRad) const {
    const geo::Transform xf = geo::Transform::rotation(pivot_, planeNormal_, angleRad);
    return xf.apply(pivot_ + rayDir_);
}

PreviewColor ProtractorTool::classifyPlaneColor() const {
    // An arrow-locked planeNormal_ dots to exactly 1.0 against its own axis,
    // so this alone covers the "tinted the axis color" case too.
    constexpr double kAxisAlignThreshold = 0.999;
    if (std::fabs(geo::dot(planeNormal_, geo::Vec3{1.0, 0.0, 0.0})) > kAxisAlignThreshold) return kAxisRedColor;
    if (std::fabs(geo::dot(planeNormal_, geo::Vec3{0.0, 1.0, 0.0})) > kAxisAlignThreshold) return kAxisGreenColor;
    if (std::fabs(geo::dot(planeNormal_, geo::Vec3{0.0, 0.0, 1.0})) > kAxisAlignThreshold) return kAxisBlueColor;
    return kDefaultPreviewColor;
}

void ProtractorTool::appendTicks(std::vector<float>& verts, double radius, const geo::Vec3& startDir) const {
    const auto [u, v] = tickBasis(planeNormal_, startDir);
    constexpr double kTickLenFrac = 0.10;      // short tick, fraction of radius
    constexpr double kLongTickLenFrac = 0.22;  // every 90 degrees (every 6th tick)
    constexpr int kCardinalEvery = kTickCount / 4;

    for (int i = 0; i < kTickCount; ++i) {
        const double angle = 2.0 * kPi * i / kTickCount;
        const geo::Vec3 dir = u * std::cos(angle) + v * std::sin(angle);
        const bool cardinal = (i % kCardinalEvery) == 0;
        const double len = radius * (cardinal ? kLongTickLenFrac : kTickLenFrac);
        appendSegment(verts, pivot_ + dir * radius, pivot_ + dir * (radius + len));
    }
}

std::vector<ToolContext::PreviewBatch> ProtractorTool::buildPreview(double angleRad, double radius) const {
    std::vector<ToolContext::PreviewBatch> batches;

    // Circle + ticks: vertex 0 (and tick 0) sit along the baseline once
    // set, else an arbitrary in-plane direction. One batch, colored via
    // classifyPlaneColor (the only piece that ever draws non-default).
    std::vector<float> circleVerts;
    const geo::Vec3 startDir = (stage_ == Stage::BaselineSet) ? rayDir_ : geo::Vec3{1.0, 0.0, 0.0};
    appendLoop(circleVerts, geo::regularPolygonPoints(pivot_, radius, planeNormal_, kCircleSegments, startDir,
                                                       /*circumscribed=*/false));
    appendTicks(circleVerts, radius, startDir);
    const PreviewColor axisColor = classifyPlaneColor();
    batches.push_back(ToolContext::PreviewBatch{std::move(circleVerts), axisColor.r, axisColor.g, axisColor.b, axisColor.a});

    if (stage_ == Stage::BaselineSet) {
        std::vector<float> rayVerts;
        appendSegment(rayVerts, pivot_, pivot_ + rayDir_);
        appendSegment(rayVerts, pivot_, sweepTip(angleRad));
        batches.push_back(ToolContext::PreviewBatch{std::move(rayVerts), kDefaultPreviewColor.r, kDefaultPreviewColor.g,
                                                      kDefaultPreviewColor.b, kDefaultPreviewColor.a});

        if (guideMode_) {
            // Dashed preview of the guide line about to be created, using the rotated baseline direction.
            const geo::Vec3 tip = sweepTip(angleRad);
            const geo::Vec3 dir = geo::normalized(tip - pivot_);
            if (geo::length(dir) > geo::kEps) {
                std::vector<float> guideVerts;
                appendDashedSegment(guideVerts, pivot_ - dir * kPreviewGuideExtent, pivot_ + dir * kPreviewGuideExtent);
                batches.push_back(
                    ToolContext::PreviewBatch{std::move(guideVerts), kGuideCueColor.r, kGuideCueColor.g, kGuideCueColor.b,
                                               kGuideCueColor.a});
            }
        }
    }

    return batches;
}

void ProtractorTool::commit(ToolContext& ctx, double angleRad) {
    // Round to the 0.1-degree cap here too -- a VCB-typed value bypasses
    // computeAngle's own rounding, so every commit routes through this.
    const double roundedDeg = roundTo(angleRad * kRadToDeg, kPrecisionDeg);
    const double roundedRad = roundedDeg * kDegToRad;

    if (guideMode_) {
        const geo::Vec3 rotatedDir = sweepTip(roundedRad) - pivot_;
        ctx.requestAddGuideLine(pivot_, rotatedDir);
        reset(ctx);
        return;
    }

    // Measure-only: freeze the reading in place (no live tracking; hint/VCB
    // hold the frozen value until Esc or a fresh click re-arms).
    frozen_ = true;
    const double radius = committedRadius();
    ctx.setPreviewBatches(buildPreview(roundedRad, radius), sweepTip(roundedRad));
    ctx.setVcbLabel("Angle");
    ctx.setVcbValue(formatExact(roundedDeg));
    ctx.setHint(currentHint());
}

void ProtractorTool::reset(ToolContext& ctx) {
    stage_ = Stage::PlaneDetect;
    lastCursor_.reset();
    shiftLock_.reset();
    altFreeze_.reset();
    frozen_ = false;
    ctx.setPreviewBatches({}, std::nullopt);
    ctx.setInferenceCue(std::nullopt);
    ctx.setHint(currentHint());
    updateVcb(ctx);
}

void ProtractorTool::updateVcb(ToolContext& ctx) const {
    ctx.setVcbLabel("Angle");
    if (stage_ != Stage::BaselineSet || frozen_ || !lastCursor_) return;
    const AngleReading reading = computeAngle(*lastCursor_, committedRadius());
    ctx.setVcbValue(formatApprox(reading.deg));
}

std::string ProtractorTool::currentHint() const {
    // Verified hint text, returned unconditionally for every stage.
    return "Ctrl = Toggle Create Guides. | Arrow Keys = Toggle Lock Rotation Plane.";
}

void ProtractorTool::showConstraintWarning(ToolContext& ctx) const {
    InferenceCue warn;
    warn.pos = lastCursor_.value_or(pivot_);
    warn.warning = true;
    warn.screenTip = "Constraint not appropriate at this time.";
    ctx.setInferenceCue(warn);
}

void ProtractorTool::handleArrowLock(ToolContext& ctx, int key) {
    if (lockedArrowKey_ == key) {
        lockedArrowKey_ = 0;
        return;
    }

    const AxesFrame frame = ctx.axesFrame();
    geo::Vec3 dir;
    if (key == Qt::Key_Up) {
        dir = frame.zDir;  // Blue
    } else if (key == Qt::Key_Right) {
        dir = frame.xDir;  // Red
    } else {
        dir = frame.yDir;  // Left = Green
    }

    arrowLockAxis_ = dir;
    arrowLockAnchor_ = lastCursor_.value_or(frame.origin);
    lockedArrowKey_ = key;
    (void)ctx;  // no immediate side effect -- the next onPointerMove picks the lock up via currentPlaneCandidate
}

void ProtractorTool::onPointerMove(ToolContext& ctx, const PointerEvent& e) {
    syncCtrl(e.ctrl);

    switch (stage_) {
        case Stage::PlaneDetect: {
            const PlaneHit hit = currentPlaneCandidate(ctx, e);
            if (hit.valid) {
                pivot_ = hit.point;
                planeNormal_ = hit.normal;
                lastCursor_ = hit.point;
            }

            std::optional<geo::Vec3> marker;
            if (hit.valid) marker = hit.point;
            ctx.setPreviewBatches(buildPreview(0.0, protractorRadius(e)), marker);

            if (lockedArrowKey_ != 0 && hit.valid) {
                InferenceCue cue;
                cue.pos = hit.point;
                cue.shape = kMarkerNone;
                cue.color = classifyPlaneColor();
                cue.screenTip = "Locked plane";
                ctx.setInferenceCue(cue);
            } else {
                ctx.setInferenceCue(std::nullopt);
            }

            ctx.setHint(currentHint());
            updateVcb(ctx);
            return;
        }
        case Stage::VertexSet: {
            const std::optional<geo::Vec3> onPlane = resolveOnFixedPlane(e);
            if (onPlane) lastCursor_ = onPlane;
            ctx.setPreviewBatches(buildPreview(0.0, protractorRadius(e)), lastCursor_);
            ctx.setHint(currentHint());
            updateVcb(ctx);
            return;
        }
        case Stage::BaselineSet: {
            if (frozen_) return;  // no live tracking while frozen -- see commit()
            const std::optional<geo::Vec3> onPlane = resolveOnFixedPlane(e);
            double angleRad = 0.0;
            if (onPlane) {
                lastCursor_ = onPlane;
                angleRad = computeAngle(*onPlane, protractorRadius(e)).rad;
            }
            ctx.setPreviewBatches(buildPreview(angleRad, protractorRadius(e)), lastCursor_);
            ctx.setHint(currentHint());
            updateVcb(ctx);
            return;
        }
    }
}

void ProtractorTool::onPointerDown(ToolContext& ctx, const PointerEvent& e) {
    syncCtrl(e.ctrl);

    if (stage_ == Stage::BaselineSet && frozen_) {
        // A click here re-arms a fresh protractor from this click; reset() drops
        // stage_ to PlaneDetect so the switch below treats it as a fresh vertex.
        reset(ctx);
    }

    switch (stage_) {
        case Stage::PlaneDetect: {
            const PlaneHit hit = currentPlaneCandidate(ctx, e);
            if (!hit.valid) return;

            pivot_ = hit.point;
            planeNormal_ = hit.normal;
            stage_ = Stage::VertexSet;
            ctx.setHint(currentHint());
            updateVcb(ctx);
            return;
        }
        case Stage::VertexSet: {
            const std::optional<geo::Vec3> onPlane = resolveOnFixedPlane(e);
            if (!onPlane) return;
            const geo::Vec3 dir = *onPlane - pivot_;
            if (geo::length(dir) <= geo::kMergeTol) {
                // Degenerate baseline (cursor back on the vertex) -- ignore, stay armed.
                return;
            }
            rayDir_ = dir;
            lastCursor_ = *onPlane;
            stage_ = Stage::BaselineSet;
            ctx.setHint(currentHint());
            updateVcb(ctx);
            return;
        }
        case Stage::BaselineSet: {
            const std::optional<geo::Vec3> onPlane = resolveOnFixedPlane(e);
            if (!onPlane) return;  // ray parallel to the protractor plane -- nothing to commit at
            commit(ctx, computeAngle(*onPlane, protractorRadius(e)).rad);
            return;
        }
    }
}

void ProtractorTool::onKeyDown(ToolContext& ctx, int key, bool /*ctrl*/) {
    if (key == Qt::Key_Escape) {
        lockedArrowKey_ = 0;  // Escape clears the arrow lock too
        reset(ctx);
        return;
    }

    if (key == Qt::Key_Up || key == Qt::Key_Right || key == Qt::Key_Left) {
        handleArrowLock(ctx, key);
        return;
    }

    if (key == Qt::Key_Down) {
        showConstraintWarning(ctx);
        return;
    }
}

void ProtractorTool::onVcbCommit(ToolContext& ctx, const VcbValue& value) {
    if (stage_ != Stage::BaselineSet) {
        // No VCB form defined for PlaneDetect/VertexSet -- no baseline to measure against yet.
        ctx.setHint("Invalid entry.");
        return;
    }

    double angleDeg = 0.0;
    switch (value.kind) {
        case VcbValue::Kind::Scalar: {
            // Magnitude from the typed value, sign from the live cursor's
            // current side when known.
            double sign = 1.0;
            if (lastCursor_) {
                const geo::Vec3 dirTo = *lastCursor_ - pivot_;
                if (geo::length(dirTo) > geo::kMergeTol) {
                    sign = signedAngle(rayDir_, dirTo, planeNormal_) < 0.0 ? -1.0 : 1.0;
                }
            }
            angleDeg = sign * std::fabs(value.a);
            break;
        }
        case VcbValue::Kind::Slope: {
            // atan(a/b) in degrees; b == 0 (vertical/undefined slope) is rejected here.
            if (std::fabs(value.b) <= geo::kEps) {
                ctx.setHint("Invalid entry.");
                return;
            }
            angleDeg = std::atan(value.a / value.b) * kRadToDeg;
            break;
        }
        default:
            ctx.setHint("Invalid entry.");
            return;
    }

    commit(ctx, angleDeg * kDegToRad);
}

}  // namespace plnr::tools
