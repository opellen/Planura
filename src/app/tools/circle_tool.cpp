#include "circle_tool.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iomanip>
#include <sstream>
#include <string>

#include <Qt>

namespace plnr::tools {

namespace {

// "~ " + value formatted to 2 decimals -- the VCB's approximate-readout
// convention (see events::VcbValueChanged's header comment).
std::string formatApprox(double value) {
    std::ostringstream oss;
    oss << "~ " << std::fixed << std::setprecision(2) << value;
    return oss.str();
}

// Turns a closed point loop into consecutive-pair + closing-pair line
// segments for ctx.setPreview -- shared by onPointerMove and onKeyDown
// (the latter redraws at the same points after a segment-count change).
std::vector<float> loopVerts(const std::vector<geo::Vec3>& points) {
    std::vector<float> verts;
    if (points.empty()) return verts;

    verts.reserve(points.size() * 6);
    for (std::size_t i = 0; i < points.size(); ++i) {
        const geo::Vec3& a = points[i];
        const geo::Vec3& b = points[(i + 1) % points.size()];
        verts.push_back(static_cast<float>(a.x));
        verts.push_back(static_cast<float>(a.y));
        verts.push_back(static_cast<float>(a.z));
        verts.push_back(static_cast<float>(b.x));
        verts.push_back(static_cast<float>(b.y));
        verts.push_back(static_cast<float>(b.z));
    }
    return verts;
}

// Ray/z=0 intersection -- mirrors geo::infer()'s GroundPlane rung. Needed
// as an explicit fallback when a HIGHER-priority hit already won infer()'s
// ladder off the ground; nullopt if the ray doesn't cross z=0 ahead.
std::optional<geo::Vec3> groundPlaneHit(const geo::Ray& ray) {
    const double denom = ray.dir.z;
    if (std::fabs(denom) < geo::kEps) return std::nullopt;
    const double t = -ray.origin.z / denom;
    if (t <= geo::kEps) return std::nullopt;
    return ray.origin + ray.dir * t;
}

}  // namespace

void CircleTool::onActivate(ToolContext& ctx) {
    segments_ = 24;
    lastCommit_.reset();
    reset(ctx);
}

void CircleTool::onDeactivate(ToolContext& ctx) {
    center_.reset();
    lastGround_.reset();
    lastCommit_.reset();
    ctx.setPreview({}, std::nullopt);
}

geo::Inference CircleTool::resolve(ToolContext& ctx, const PointerEvent& e) const {
    const geo::Model* model = ctx.model();
    if (!model) return geo::Inference{};

    geo::Inference inf = geo::infer(*model, e.ray, geo::InferenceContext{std::nullopt, std::nullopt, e.tols});
    // Ground-plane-only: an on-ground hit keeps z snapped to 0.0; an
    // off-ground hit must NOT be flattened in place -- falls back to the
    // click ray's z=0 crossing instead (flagged-traps-tools-A.md).
    if (inf.kind != geo::InferenceKind::None && std::fabs(inf.pos.z) > geo::kMergeTol) {
        const std::optional<geo::Vec3> ground = groundPlaneHit(e.ray);
        return ground ? geo::Inference{geo::InferenceKind::GroundPlane, *ground} : geo::Inference{};
    }
    inf.pos.z = 0.0;
    return inf;
}

std::vector<geo::Vec3> CircleTool::pointsForRadius(const geo::Vec3& center, double radius, const geo::Vec3& dir,
                                                    int segments) const {
    return geo::regularPolygonPoints(center, radius, geo::Vec3{0.0, 0.0, 1.0}, segments, dir,
                                      /*circumscribed=*/false);
}

std::vector<geo::Vec3> CircleTool::pointsFor(const geo::Vec3& center, const geo::Vec3& cursor) const {
    return pointsForRadius(center, geo::distance(cursor, center), cursor - center, segments_);
}

void CircleTool::reset(ToolContext& ctx) {
    center_.reset();
    lastGround_.reset();
    ctx.setPreview({}, std::nullopt);
    ctx.setHint(kActivationHint);
    updateVcb(ctx);
}

void CircleTool::updateVcb(ToolContext& ctx) const {
    if (!center_) {
        ctx.setVcbLabel("Sides");
        ctx.setVcbValue(std::to_string(segments_));
        return;
    }
    ctx.setVcbLabel("Radius");
    if (lastGround_) {
        ctx.setVcbValue(formatApprox(geo::distance(*lastGround_, *center_)));
    }
}

void CircleTool::onPointerMove(ToolContext& ctx, const PointerEvent& e) {
    const geo::Inference inf = resolve(ctx, e);
    const bool hasTarget = inf.kind != geo::InferenceKind::None;
    if (hasTarget) lastGround_ = inf.pos;

    std::optional<geo::Vec3> marker;
    if (hasTarget) marker = inf.pos;

    std::vector<float> lineVerts;
    if (center_ && hasTarget) {
        lineVerts = loopVerts(pointsFor(*center_, inf.pos));
    }
    ctx.setPreview(std::move(lineVerts), marker);

    ctx.setHint(center_ ? "Select radius point." : kActivationHint);
    updateVcb(ctx);
}

void CircleTool::onPointerDown(ToolContext& ctx, const PointerEvent& e) {
    const geo::Inference inf = resolve(ctx, e);
    if (inf.kind == geo::InferenceKind::None) return;

    if (!center_) {
        center_ = inf.pos;
        lastGround_ = inf.pos;
        // Starting a new circle invalidates any prior retro-edit window --
        // a follow-up typed radius/segment-count must apply to THIS new
        // shape once it's committed, never reach back to the last one.
        lastCommit_.reset();
        return;
    }

    if (geo::distance(inf.pos, *center_) < geo::kMergeTol) {
        // Degenerate radius (same point re-clicked) -- ignore, stay armed
        // at the center, matching addPolyline's own points.size()<2
        // rejection.
        return;
    }

    const geo::Vec3 dir = inf.pos - *center_;
    const double radius = geo::distance(inf.pos, *center_);
    ctx.requestAddPolyline(pointsForRadius(*center_, radius, dir, segments_), /*closed=*/true);
    lastCommit_ = LastCommit{*center_, dir, radius, segments_};
    reset(ctx);
}

void CircleTool::onVcbCommit(ToolContext& ctx, const VcbValue& value) {
    if (!center_) {
        if (!lastCommit_) {
            // Idle, nothing to retro-edit -- Scalar/Segments/CircleSegments
            // set segments_ for the NEXT circle; Radius/Dims2 have no
            // meaning without a center.
            switch (value.kind) {
                case VcbValue::Kind::Scalar:
                    if (value.a != std::floor(value.a) || value.a < kMinSegments) {
                        ctx.setHint("Invalid entry.");
                        return;
                    }
                    segments_ = std::clamp(static_cast<int>(value.a), kMinSegments, kMaxSegments);
                    break;
                case VcbValue::Kind::Segments:
                case VcbValue::Kind::CircleSegments:
                    segments_ = std::clamp(value.count, kMinSegments, kMaxSegments);
                    break;
                case VcbValue::Kind::Radius:
                case VcbValue::Kind::Dims2:
                    ctx.setHint("Invalid entry.");
                    return;
            }
            updateVcb(ctx);
            return;
        }

        // Idle with an open retro-edit window: regenerate the just-
        // committed circle in place instead of starting a new one.
        switch (value.kind) {
            case VcbValue::Kind::Scalar:
            case VcbValue::Kind::Radius: {
                const double radius = value.a;
                if (radius <= geo::kMergeTol) {
                    ctx.setHint("Invalid entry.");
                    return;
                }
                const std::vector<geo::Vec3> pts =
                    pointsForRadius(lastCommit_->center, radius, lastCommit_->dir, segments_);
                if (pts.empty()) {
                    ctx.setHint("Invalid entry.");
                    return;
                }
                ctx.requestReplaceLastPolyline(pts, /*closed=*/true);
                lastCommit_->radius = radius;
                return;
            }
            case VcbValue::Kind::Segments:
            case VcbValue::Kind::CircleSegments: {
                segments_ = std::clamp(value.count, kMinSegments, kMaxSegments);
                const std::vector<geo::Vec3> pts =
                    pointsForRadius(lastCommit_->center, lastCommit_->radius, lastCommit_->dir, segments_);
                if (pts.empty()) {
                    ctx.setHint("Invalid entry.");
                    return;
                }
                ctx.requestReplaceLastPolyline(pts, /*closed=*/true);
                lastCommit_->segments = segments_;
                return;
            }
            case VcbValue::Kind::Dims2:
                ctx.setHint("Invalid entry.");
                return;
        }
        return;
    }

    // Rubber-band stage: center_ is placed, nothing committed yet.
    switch (value.kind) {
        case VcbValue::Kind::Scalar:
        case VcbValue::Kind::Radius: {
            const double radius = value.a;
            if (radius <= geo::kMergeTol) {
                ctx.setHint("Invalid entry.");
                return;
            }
            const geo::Vec3 dir = lastGround_ ? (*lastGround_ - *center_) : geo::Vec3{1.0, 0.0, 0.0};
            const std::vector<geo::Vec3> pts = pointsForRadius(*center_, radius, dir, segments_);
            if (pts.empty()) {
                ctx.setHint("Invalid entry.");
                return;
            }
            ctx.requestAddPolyline(pts, /*closed=*/true);
            lastCommit_ = LastCommit{*center_, dir, radius, segments_};
            reset(ctx);
            return;
        }
        case VcbValue::Kind::Segments:
        case VcbValue::Kind::CircleSegments:
            segments_ = std::clamp(value.count, kMinSegments, kMaxSegments);
            if (lastGround_) ctx.setPreview(loopVerts(pointsFor(*center_, *lastGround_)), *lastGround_);
            updateVcb(ctx);
            return;
        case VcbValue::Kind::Dims2:
            ctx.setHint("Invalid entry.");
            return;
    }
}

void CircleTool::onKeyDown(ToolContext& ctx, int key, bool ctrl) {
    if (key == Qt::Key_Escape) {
        lastCommit_.reset();
        reset(ctx);
        return;
    }

    // the reference modeler requires Ctrl held for '+'/'-' to resize the segment count.
    // Key_Equal covers '+' without shift on layouts where that's a
    // separate keycode from the numpad Plus.
    if (!ctrl) return;
    if (key == Qt::Key_Plus || key == Qt::Key_Equal) {
        segments_ = std::min(segments_ + 1, kMaxSegments);
    } else if (key == Qt::Key_Minus) {
        segments_ = std::max(segments_ - 1, kMinSegments);
    } else {
        return;
    }

    if (center_ && lastGround_) {
        ctx.setPreview(loopVerts(pointsFor(*center_, *lastGround_)), *lastGround_);
    }
    updateVcb(ctx);
}

}  // namespace plnr::tools
