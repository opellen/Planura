#include "polygon_tool.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iomanip>
#include <sstream>
#include <string>

#include <Qt>

namespace plnr::tools {

namespace {

// "~ " + value to 2 decimals: the VCB's approximate-readout convention.
std::string formatApprox(double value) {
    std::ostringstream oss;
    oss << "~ " << std::fixed << std::setprecision(2) << value;
    return oss.str();
}

// Closed point loop -> consecutive + closing segments for ctx.setPreview (shared by onPointerMove/onKeyDown).
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

// Ray/z=0 intersection (mirrors infer()'s GroundPlane rung), the fallback when a higher-priority hit won; nullopt if the ray misses z=0 ahead.
std::optional<geo::Vec3> groundPlaneHit(const geo::Ray& ray) {
    const double denom = ray.dir.z;
    if (std::fabs(denom) < geo::kEps) return std::nullopt;
    const double t = -ray.origin.z / denom;
    if (t <= geo::kEps) return std::nullopt;
    return ray.origin + ray.dir * t;
}

}  // namespace

void PolygonTool::onActivate(ToolContext& ctx) {
    segments_ = 6;
    ctx.setSegments(segments_);
    lastCommit_.reset();
    reset(ctx);
}

void PolygonTool::onDeactivate(ToolContext& ctx) {
    center_.reset();
    lastGround_.reset();
    lastCommit_.reset();
    ctx.setPreview({}, std::nullopt);
}

geo::Inference PolygonTool::resolve(ToolContext& ctx, const PointerEvent& e) const {
    const geo::Model* model = ctx.model();
    if (!model) return geo::Inference{};

    geo::Inference inf = geo::infer(*model, e.ray, geo::InferenceContext{std::nullopt, std::nullopt, e.tols});
    // Ground-plane only: an on-ground hit keeps z = 0; an off-ground hit is NOT flattened, it falls
    // back to the click ray's z=0 crossing.
    if (inf.kind != geo::InferenceKind::None && std::fabs(inf.pos.z) > geo::kMergeTol) {
        const std::optional<geo::Vec3> ground = groundPlaneHit(e.ray);
        return ground ? geo::Inference{geo::InferenceKind::GroundPlane, *ground} : geo::Inference{};
    }
    inf.pos.z = 0.0;
    return inf;
}

std::vector<geo::Vec3> PolygonTool::pointsForRadius(const geo::Vec3& center, double radius, const geo::Vec3& dir,
                                                     int segments) const {
    return geo::regularPolygonPoints(center, radius, geo::Vec3{0.0, 0.0, 1.0}, segments, dir,
                                      /*circumscribed=*/false);
}

std::vector<geo::Vec3> PolygonTool::pointsFor(const geo::Vec3& center, const geo::Vec3& cursor) const {
    return pointsForRadius(center, geo::distance(cursor, center), cursor - center, segments_);
}

void PolygonTool::reset(ToolContext& ctx) {
    center_.reset();
    lastGround_.reset();
    ctx.setPreview({}, std::nullopt);
    ctx.setHint(kActivationHint);
    updateVcb(ctx);
}

void PolygonTool::updateVcb(ToolContext& ctx) const {
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

void PolygonTool::onPointerMove(ToolContext& ctx, const PointerEvent& e) {
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

void PolygonTool::onPointerDown(ToolContext& ctx, const PointerEvent& e) {
    const geo::Inference inf = resolve(ctx, e);
    if (inf.kind == geo::InferenceKind::None) return;

    if (!center_) {
        center_ = inf.pos;
        lastGround_ = inf.pos;
        // A new polygon invalidates the prior retro-edit window.
        lastCommit_.reset();
        return;
    }

    if (geo::distance(inf.pos, *center_) < geo::kMergeTol) {
        // Degenerate radius (same point re-clicked): ignore, stay armed at the center.
        return;
    }

    const geo::Vec3 dir = inf.pos - *center_;
    const double radius = geo::distance(inf.pos, *center_);
    ctx.requestAddPolyline(pointsForRadius(*center_, radius, dir, segments_), /*closed=*/true);
    lastCommit_ = LastCommit{*center_, dir, radius, segments_};
    reset(ctx);
}

void PolygonTool::onVcbCommit(ToolContext& ctx, const VcbValue& value) {
    if (!center_) {
        if (!lastCommit_) {
            // Idle, nothing to retro-edit: Scalar/Segments/CircleSegments set segments_ for the next polygon.
            switch (value.kind) {
                case VcbValue::Kind::Scalar:
                    if (value.a != std::floor(value.a) || value.a < kMinSegments) {
                        ctx.setHint("Invalid entry.");
                        return;
                    }
                    segments_ = std::clamp(static_cast<int>(value.a), kMinSegments, kMaxSegments);
                    ctx.setSegments(segments_);
                    break;
                case VcbValue::Kind::Segments:
                case VcbValue::Kind::CircleSegments:
                    segments_ = std::clamp(value.count, kMinSegments, kMaxSegments);
                    ctx.setSegments(segments_);
                    break;
                case VcbValue::Kind::Radius:
                case VcbValue::Kind::Dims2:
                    ctx.setHint("Invalid entry.");
                    return;
            }
            updateVcb(ctx);
            return;
        }

        // Idle with an open retro-edit window: regenerate the last polygon instead of starting a new one.
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
                ctx.setSegments(segments_);
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
            ctx.setSegments(segments_);
            if (lastGround_) ctx.setPreview(loopVerts(pointsFor(*center_, *lastGround_)), *lastGround_);
            updateVcb(ctx);
            return;
        case VcbValue::Kind::Dims2:
            ctx.setHint("Invalid entry.");
            return;
    }
}

void PolygonTool::onKeyDown(ToolContext& ctx, int key, bool ctrl) {
    if (key == Qt::Key_Escape) {
        lastCommit_.reset();
        reset(ctx);
        return;
    }

    // the reference modeler needs Ctrl for '+'/'-' side resize; Key_Equal is '+' without shift.
    if (!ctrl) return;
    if (key == Qt::Key_Plus || key == Qt::Key_Equal) {
        segments_ = std::min(segments_ + 1, kMaxSegments);
        ctx.setSegments(segments_);
    } else if (key == Qt::Key_Minus) {
        segments_ = std::max(segments_ - 1, kMinSegments);
        ctx.setSegments(segments_);
    } else {
        return;
    }

    if (center_ && lastGround_) {
        ctx.setPreview(loopVerts(pointsFor(*center_, *lastGround_)), *lastGround_);
    }
    updateVcb(ctx);
}

}  // namespace plnr::tools
