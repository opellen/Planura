#include "arc_center_tool.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iomanip>
#include <sstream>
#include <string>

#include <Qt>

namespace plnr::tools {

namespace {

// Degrees per radian for the VCB's Angle readout -- sweep math (signedAngle) stays in radians.
inline constexpr double kRadToDeg = 180.0 / 3.14159265358979323846;
inline constexpr double kDegToRad = 3.14159265358979323846 / 180.0;
inline constexpr double kTwoPi = 2.0 * 3.14159265358979323846;

// "~ " + value formatted to 2 decimals -- the VCB's approximate-readout
// convention (see events::VcbValueChanged's header comment).
std::string formatApprox(double value) {
    std::ostringstream oss;
    oss << "~ " << std::fixed << std::setprecision(2) << value;
    return oss.str();
}

// Turns an open point chain into consecutive-pair line segments -- unlike
// Circle/Polygon's loopVerts, never closes back to the first point.
std::vector<float> chainVerts(const std::vector<geo::Vec3>& points) {
    std::vector<float> verts;
    if (points.size() < 2) return verts;

    verts.reserve((points.size() - 1) * 6);
    for (std::size_t i = 0; i + 1 < points.size(); ++i) {
        const geo::Vec3& a = points[i];
        const geo::Vec3& b = points[i + 1];
        verts.push_back(static_cast<float>(a.x));
        verts.push_back(static_cast<float>(a.y));
        verts.push_back(static_cast<float>(a.z));
        verts.push_back(static_cast<float>(b.x));
        verts.push_back(static_cast<float>(b.y));
        verts.push_back(static_cast<float>(b.z));
    }
    return verts;
}

// Ray/z=0 intersection -- mirrors geo::infer()'s GroundPlane rung, needed as
// a fallback when a HIGHER-priority hit already won infer()'s ladder.
std::optional<geo::Vec3> groundPlaneHit(const geo::Ray& ray) {
    const double denom = ray.dir.z;
    if (std::fabs(denom) < geo::kEps) return std::nullopt;
    const double t = -ray.origin.z / denom;
    if (t <= geo::kEps) return std::nullopt;
    return ray.origin + ray.dir * t;
}

// Signed angle (radians, in (-pi, pi]) from `from` to `to` about unit axis
// `normal`, scale-invariant -- used by ArcCenterTool/PieTool for the cursor's
// live sweep. No continuity past +-pi across moves (MVP: re-derived each move).
double signedAngle(const geo::Vec3& from, const geo::Vec3& to, const geo::Vec3& normal) {
    const double sinPart = geo::dot(normal, geo::cross(from, to));
    const double cosPart = geo::dot(from, to);
    return std::atan2(sinPart, cosPart);
}

}  // namespace

void ArcCenterTool::onActivate(ToolContext& ctx) {
    segments_ = geo::kArcDefaultSegments;
    lastCommit_.reset();
    reset(ctx);
}

void ArcCenterTool::onDeactivate(ToolContext& ctx) {
    center_.reset();
    startPoint_.reset();
    lastGround_.reset();
    lastCommit_.reset();
    ctx.setPreview({}, std::nullopt);
}

geo::Inference ArcCenterTool::resolve(ToolContext& ctx, const PointerEvent& e) const {
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

std::vector<geo::Vec3> ArcCenterTool::pointsForSweep(const geo::Vec3& center, const geo::Vec3& startPoint,
                                                      double sweep, int segments) const {
    static constexpr geo::Vec3 kNormal{0.0, 0.0, 1.0};
    return geo::arcPointsCenter(center, startPoint, sweep, kNormal, segments);
}

std::vector<geo::Vec3> ArcCenterTool::pointsFor(const geo::Vec3& center, const geo::Vec3& startPoint,
                                                 const geo::Vec3& cursor) const {
    static constexpr geo::Vec3 kNormal{0.0, 0.0, 1.0};

    const geo::Vec3 dirFrom = startPoint - center;
    const geo::Vec3 dirTo = cursor - center;
    if (geo::length(dirTo) <= geo::kMergeTol) return {};  // cursor on the center -- no direction to sweep toward

    const double sweep = signedAngle(dirFrom, dirTo, kNormal);
    return pointsForSweep(center, startPoint, sweep, segments_);
}

void ArcCenterTool::commitSweep(ToolContext& ctx, double sweep) {
    const std::vector<geo::Vec3> pts = pointsForSweep(*center_, *startPoint_, sweep, segments_);
    if (pts.empty()) {
        ctx.setHint("Invalid entry.");
        return;
    }
    ctx.requestAddPolyline(pts, /*closed=*/false);
    lastCommit_ = LastCommit{*center_, *startPoint_, sweep, segments_};
    reset(ctx);
}

void ArcCenterTool::regenerate(ToolContext& ctx, double sweep, int segments) {
    const std::vector<geo::Vec3> pts = pointsForSweep(lastCommit_->center, lastCommit_->startPoint, sweep, segments);
    if (pts.empty()) {
        ctx.setHint("Invalid entry.");
        return;
    }
    ctx.requestReplaceLastPolyline(pts, /*closed=*/false);
    lastCommit_->sweep = sweep;
    lastCommit_->segments = segments;
}

std::vector<float> ArcCenterTool::previewVerts(const geo::Vec3& cursor) const {
    if (center_ && startPoint_) {
        return chainVerts(pointsFor(*center_, *startPoint_, cursor));
    }
    if (center_) {
        return chainVerts({*center_, cursor});
    }
    return {};
}

void ArcCenterTool::reset(ToolContext& ctx) {
    center_.reset();
    startPoint_.reset();
    lastGround_.reset();
    ctx.setPreview({}, std::nullopt);
    ctx.setHint(kActivationHint);
    updateVcb(ctx);
}

void ArcCenterTool::updateVcb(ToolContext& ctx) const {
    if (!startPoint_) {
        ctx.setVcbLabel("Radius");
        if (center_ && lastGround_) ctx.setVcbValue(formatApprox(geo::distance(*center_, *lastGround_)));
        return;
    }
    ctx.setVcbLabel("Angle");
    if (lastGround_) {
        static constexpr geo::Vec3 kNormal{0.0, 0.0, 1.0};
        const double sweepDeg = signedAngle(*startPoint_ - *center_, *lastGround_ - *center_, kNormal) * kRadToDeg;
        ctx.setVcbValue(formatApprox(sweepDeg));
    }
}

void ArcCenterTool::onPointerMove(ToolContext& ctx, const PointerEvent& e) {
    const geo::Inference inf = resolve(ctx, e);
    const bool hasTarget = inf.kind != geo::InferenceKind::None;
    if (hasTarget) lastGround_ = inf.pos;

    std::optional<geo::Vec3> marker;
    if (hasTarget) marker = inf.pos;

    std::vector<float> lineVerts;
    if (hasTarget) lineVerts = previewVerts(inf.pos);
    ctx.setPreview(std::move(lineVerts), marker);

    if (!center_) {
        ctx.setHint(kActivationHint);
    } else if (!startPoint_) {
        ctx.setHint(kStartHint);
    } else {
        ctx.setHint(kSweepHint);
    }
    updateVcb(ctx);
}

void ArcCenterTool::onPointerDown(ToolContext& ctx, const PointerEvent& e) {
    const geo::Inference inf = resolve(ctx, e);
    if (inf.kind == geo::InferenceKind::None) return;

    if (!center_) {
        center_ = inf.pos;
        lastGround_ = inf.pos;
        // Starting a new arc invalidates any prior retro-edit window -- a
        // follow-up edit must apply to THIS arc, not the last one.
        lastCommit_.reset();
        return;
    }

    if (!startPoint_) {
        if (geo::distance(inf.pos, *center_) < geo::kMergeTol) {
            // Degenerate radius (same point re-clicked) -- ignore, stay
            // armed at center_ (Circle/Polygon's own guard).
            return;
        }
        startPoint_ = inf.pos;
        lastGround_ = inf.pos;
        return;
    }

    static constexpr geo::Vec3 kNormal{0.0, 0.0, 1.0};
    const geo::Vec3 dirTo = inf.pos - *center_;
    if (geo::length(dirTo) <= geo::kMergeTol) return;  // cursor on the center -- no direction to sweep toward
    const double sweep = signedAngle(*startPoint_ - *center_, dirTo, kNormal);
    const std::vector<geo::Vec3> points = pointsForSweep(*center_, *startPoint_, sweep, segments_);
    if (points.empty()) {
        // Degenerate sweep (too small for arcPointsCenter's own guard) --
        // ignore, stay armed at the start point.
        return;
    }

    ctx.requestAddPolyline(points, /*closed=*/false);
    lastCommit_ = LastCommit{*center_, *startPoint_, sweep, segments_};
    reset(ctx);
}

void ArcCenterTool::onVcbCommit(ToolContext& ctx, const VcbValue& value) {
    if (!center_) {
        if (lastCommit_) {
            // Idle with an open retro-edit window: regenerate the just-
            // committed arc in place instead of starting a new one.
            switch (value.kind) {
                case VcbValue::Kind::Scalar: {
                    // Angle in DEGREES, sign taken directly (no cursor here to force a side).
                    regenerate(ctx, value.a * kDegToRad, segments_);
                    return;
                }
                case VcbValue::Kind::Radius: {
                    const double radius = value.a;
                    if (radius <= geo::kMergeTol) {
                        ctx.setHint("Invalid entry.");
                        return;
                    }
                    static constexpr geo::Vec3 kNormal{0.0, 0.0, 1.0};
                    const geo::Vec3 dir = lastCommit_->startPoint - lastCommit_->center;
                    if (geo::length(dir) <= geo::kMergeTol) {
                        ctx.setHint("Invalid entry.");
                        return;
                    }
                    const geo::Vec3 newStart = lastCommit_->center + geo::normalized(dir) * radius;
                    const std::vector<geo::Vec3> pts =
                        pointsForSweep(lastCommit_->center, newStart, lastCommit_->sweep, segments_);
                    if (pts.empty()) {
                        ctx.setHint("Invalid entry.");
                        return;
                    }
                    ctx.requestReplaceLastPolyline(pts, /*closed=*/false);
                    lastCommit_->startPoint = newStart;
                    return;
                }
                case VcbValue::Kind::Segments:
                    segments_ = std::clamp(value.count, kMinSegments, kMaxSegments);
                    regenerate(ctx, lastCommit_->sweep, segments_);
                    return;
                case VcbValue::Kind::CircleSegments: {
                    const double sweepAbs = std::fabs(lastCommit_->sweep);
                    segments_ = std::max(1, static_cast<int>(std::lround(value.count * sweepAbs / kTwoPi)));
                    regenerate(ctx, lastCommit_->sweep, segments_);
                    return;
                }
                case VcbValue::Kind::Dims2:
                    ctx.setHint("Invalid entry.");
                    return;
            }
            return;
        }

        // Idle, nothing to retro-edit -- Segments/CircleSegments still
        // adjust segments_ for the NEXT arc (mirroring Ctrl+/-).
        switch (value.kind) {
            case VcbValue::Kind::Segments:
            case VcbValue::Kind::CircleSegments:
                segments_ = std::clamp(value.count, kMinSegments, kMaxSegments);
                updateVcb(ctx);
                return;
            case VcbValue::Kind::Scalar:
            case VcbValue::Kind::Radius:
            case VcbValue::Kind::Dims2:
                ctx.setHint("Invalid entry.");
                return;
        }
        return;
    }

    if (!startPoint_) {
        // Radius stage: center_ placed, advancing to the arc's start point.
        switch (value.kind) {
            case VcbValue::Kind::Scalar:
            case VcbValue::Kind::Radius: {
                if (!lastGround_) {
                    ctx.setHint("Invalid entry.");
                    return;
                }
                const double radius = value.a;
                if (radius <= geo::kMergeTol) {
                    ctx.setHint("Invalid entry.");
                    return;
                }
                const geo::Vec3 dirTo = *lastGround_ - *center_;
                if (geo::length(dirTo) <= geo::kMergeTol) {
                    ctx.setHint("Invalid entry.");
                    return;
                }
                startPoint_ = *center_ + geo::normalized(dirTo) * radius;
                lastGround_ = startPoint_;
                ctx.setPreview(previewVerts(*startPoint_), *startPoint_);
                ctx.setHint(kSweepHint);
                updateVcb(ctx);
                return;
            }
            case VcbValue::Kind::Segments:
            case VcbValue::Kind::CircleSegments:
                segments_ = std::clamp(value.count, kMinSegments, kMaxSegments);
                if (lastGround_) ctx.setPreview(previewVerts(*lastGround_), *lastGround_);
                updateVcb(ctx);
                return;
            case VcbValue::Kind::Dims2:
                ctx.setHint("Invalid entry.");
                return;
        }
        return;
    }

    // Sweep stage: center_ and startPoint_ both placed, nothing committed yet.
    switch (value.kind) {
        case VcbValue::Kind::Scalar: {
            static constexpr geo::Vec3 kNormal{0.0, 0.0, 1.0};
            double sweep = value.a * kDegToRad;
            if (lastGround_) {
                const double cursorSweep = signedAngle(*startPoint_ - *center_, *lastGround_ - *center_, kNormal);
                const double sign = cursorSweep < 0.0 ? -1.0 : 1.0;
                sweep = sign * std::fabs(sweep);
            }
            commitSweep(ctx, sweep);
            return;
        }
        case VcbValue::Kind::Segments:
        case VcbValue::Kind::CircleSegments:
            segments_ = std::clamp(value.count, kMinSegments, kMaxSegments);
            if (lastGround_) ctx.setPreview(previewVerts(*lastGround_), *lastGround_);
            updateVcb(ctx);
            return;
        case VcbValue::Kind::Radius:
        case VcbValue::Kind::Dims2:
            ctx.setHint("Invalid entry.");
            return;
    }
}

void ArcCenterTool::onKeyDown(ToolContext& ctx, int key, bool ctrl) {
    if (key == Qt::Key_Escape) {
        lastCommit_.reset();
        reset(ctx);
        return;
    }

    // the reference modeler requires Ctrl held for '+'/'-'; Key_Equal covers '+' without
    // shift on layouts where it's a separate keycode from numpad Plus.
    if (!ctrl) return;
    if (key == Qt::Key_Plus || key == Qt::Key_Equal) {
        segments_ = std::min(segments_ + 1, kMaxSegments);
    } else if (key == Qt::Key_Minus) {
        segments_ = std::max(segments_ - 1, kMinSegments);
    } else {
        return;
    }

    if (lastGround_) {
        ctx.setPreview(previewVerts(*lastGround_), *lastGround_);
    }
    updateVcb(ctx);
}

}  // namespace plnr::tools
