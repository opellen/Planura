#include "arc2point_tool.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iomanip>
#include <sstream>
#include <string>

#include <Qt>

namespace plnr::tools {

namespace {

inline constexpr double kTwoPi = 2.0 * 3.14159265358979323846;

// "~ " + value to 2 decimals: the VCB's approximate-readout convention.
std::string formatApprox(double value) {
    std::ostringstream oss;
    oss << "~ " << std::fixed << std::setprecision(2) << value;
    return oss.str();
}

// Open point chain -> consecutive-pair segments for ctx.setPreview; never closes (arc commits are open polylines).
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

// Ray/z=0 intersection (mirrors infer()'s GroundPlane rung), the fallback when a higher-priority hit won off the ground; nullopt if the ray misses z=0 ahead.
std::optional<geo::Vec3> groundPlaneHit(const geo::Ray& ray) {
    const double denom = ray.dir.z;
    if (std::fabs(denom) < geo::kEps) return std::nullopt;
    const double t = -ray.origin.z / denom;
    if (t <= geo::kEps) return std::nullopt;
    return ray.origin + ray.dir * t;
}

}  // namespace

void Arc2PointTool::onActivate(ToolContext& ctx) {
    segments_ = geo::kArcDefaultSegments;
    ctx.setSegments(segments_);
    lastCommit_.reset();
    reset(ctx);
}

void Arc2PointTool::onDeactivate(ToolContext& ctx) {
    start_.reset();
    chordEnd_.reset();
    lastGround_.reset();
    lastCommit_.reset();
    ctx.setPreview({}, std::nullopt);
}

geo::Inference Arc2PointTool::resolve(ToolContext& ctx, const PointerEvent& e) const {
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

double Arc2PointTool::bulgeFor(const geo::Vec3& a, const geo::Vec3& b, const geo::Vec3& cursor) const {
    static constexpr geo::Vec3 kNormal{0.0, 0.0, 1.0};

    const geo::Vec3 chord = b - a;
    const double chordLen = geo::length(chord);
    if (chordLen <= geo::kMergeTol) return 0.0;  // avoid dividing by a near-zero chord below

    // Cursor projected onto the chord line: offset magnitude = |bulge|, side (vs. cross(normal, chord)) = sign.
    const geo::Vec3 chordDir = chord * (1.0 / chordLen);
    const geo::Vec3 bulgeAxis = geo::cross(kNormal, chordDir);  // unit: kNormal and chordDir are always perpendicular on the ground plane
    const double alongLen = geo::dot(cursor - a, chordDir);
    const geo::Vec3 projected = a + chordDir * alongLen;
    return geo::dot(cursor - projected, bulgeAxis);
}

std::vector<geo::Vec3> Arc2PointTool::pointsForBulge(const geo::Vec3& a, const geo::Vec3& b, double bulge,
                                                       int segments) const {
    static constexpr geo::Vec3 kNormal{0.0, 0.0, 1.0};
    return geo::arcPoints2Point(a, b, bulge, kNormal, segments);
}

std::vector<geo::Vec3> Arc2PointTool::pointsFor(const geo::Vec3& a, const geo::Vec3& b,
                                                 const geo::Vec3& cursor) const {
    // A degenerate chord yields 0.0 here; arcPoints2Point rejects it anyway.
    return pointsForBulge(a, b, bulgeFor(a, b, cursor), segments_);
}

std::optional<double> Arc2PointTool::sagittaForRadius(double chordLen, double r) const {
    const double h = chordLen * 0.5;
    if (r < h) return std::nullopt;  // minor-arc MVP guard: no minor arc at this radius spans this chord
    return r - std::sqrt(r * r - h * h);
}

double Arc2PointTool::sweepForChordBulge(double chordLen, double absBulge) const {
    const double h = chordLen * 0.5;
    const double s = absBulge;
    if (s <= geo::kEps) return 0.0;
    const double r = (h * h + s * s) / (2.0 * s);
    return 2.0 * std::atan2(h, r - s);
}

void Arc2PointTool::commitBulge(ToolContext& ctx, double bulge) {
    const std::vector<geo::Vec3> pts = pointsForBulge(*start_, *chordEnd_, bulge, segments_);
    if (pts.empty()) {
        ctx.setHint("Invalid entry.");
        return;
    }
    ctx.requestAddPolyline(pts, /*closed=*/false);
    lastCommit_ = LastCommit{*start_, *chordEnd_, bulge, segments_};
    reset(ctx);
}

void Arc2PointTool::regenerate(ToolContext& ctx, double bulge, int segments) {
    const std::vector<geo::Vec3> pts = pointsForBulge(lastCommit_->a, lastCommit_->b, bulge, segments);
    if (pts.empty()) {
        ctx.setHint("Invalid entry.");
        return;
    }
    ctx.requestReplaceLastPolyline(pts, /*closed=*/false);
    lastCommit_->bulge = bulge;
    lastCommit_->segments = segments;
}

std::vector<float> Arc2PointTool::previewVerts(const geo::Vec3& cursor) const {
    if (start_ && chordEnd_) {
        return chainVerts(pointsFor(*start_, *chordEnd_, cursor));
    }
    if (start_) {
        return chainVerts({*start_, cursor});
    }
    return {};
}

void Arc2PointTool::reset(ToolContext& ctx) {
    start_.reset();
    chordEnd_.reset();
    lastGround_.reset();
    ctx.setPreview({}, std::nullopt);
    ctx.setHint(kActivationHint);
    updateVcb(ctx);
}

void Arc2PointTool::updateVcb(ToolContext& ctx) const {
    if (!chordEnd_) {
        ctx.setVcbLabel("Length");
        if (start_ && lastGround_) ctx.setVcbValue(formatApprox(geo::distance(*start_, *lastGround_)));
        return;
    }
    ctx.setVcbLabel("Bulge");
    if (lastGround_) ctx.setVcbValue(formatApprox(bulgeFor(*start_, *chordEnd_, *lastGround_)));
}

void Arc2PointTool::onPointerMove(ToolContext& ctx, const PointerEvent& e) {
    const geo::Inference inf = resolve(ctx, e);
    const bool hasTarget = inf.kind != geo::InferenceKind::None;
    if (hasTarget) lastGround_ = inf.pos;

    std::optional<geo::Vec3> marker;
    if (hasTarget) marker = inf.pos;

    std::vector<float> lineVerts;
    if (hasTarget) lineVerts = previewVerts(inf.pos);
    ctx.setPreview(std::move(lineVerts), marker);

    if (!start_) {
        ctx.setHint(kActivationHint);
    } else if (!chordEnd_) {
        ctx.setHint(kChordHint);
    } else {
        ctx.setHint(kBulgeHint);
    }
    updateVcb(ctx);
}

void Arc2PointTool::onPointerDown(ToolContext& ctx, const PointerEvent& e) {
    const geo::Inference inf = resolve(ctx, e);
    if (inf.kind == geo::InferenceKind::None) return;

    if (!start_) {
        start_ = inf.pos;
        lastGround_ = inf.pos;
        // A new arc invalidates the prior retro-edit window.
        lastCommit_.reset();
        return;
    }

    if (!chordEnd_) {
        if (geo::distance(inf.pos, *start_) < geo::kMergeTol) {
            // Degenerate chord (same point re-clicked): ignore, stay armed at start_.
            return;
        }
        chordEnd_ = inf.pos;
        lastGround_ = inf.pos;
        return;
    }

    const double bulge = bulgeFor(*start_, *chordEnd_, inf.pos);
    const std::vector<geo::Vec3> points = pointsForBulge(*start_, *chordEnd_, bulge, segments_);
    if (points.empty()) {
        // Degenerate bulge (cursor on the chord line): ignore, stay armed.
        return;
    }

    ctx.requestAddPolyline(points, /*closed=*/false);
    lastCommit_ = LastCommit{*start_, *chordEnd_, bulge, segments_};
    reset(ctx);
}

void Arc2PointTool::onVcbCommit(ToolContext& ctx, const VcbValue& value) {
    if (!start_) {
        if (lastCommit_) {
            // Idle with an open retro-edit window: regenerate the last arc instead of starting a new one.
            switch (value.kind) {
                case VcbValue::Kind::Scalar:
                case VcbValue::Kind::Radius: {
                    const double chordLen = geo::distance(lastCommit_->a, lastCommit_->b);
                    const std::optional<double> mag =
                        value.kind == VcbValue::Kind::Radius ? sagittaForRadius(chordLen, value.a)
                                                              : std::make_optional(std::fabs(value.a));
                    if (!mag || *mag <= geo::kMergeTol) {
                        ctx.setHint("Invalid entry.");
                        return;
                    }
                    const double sign = lastCommit_->bulge < 0.0 ? -1.0 : 1.0;
                    regenerate(ctx, sign * (*mag), segments_);
                    return;
                }
                case VcbValue::Kind::Segments:
                    segments_ = std::clamp(value.count, kMinSegments, kMaxSegments);
                    ctx.setSegments(segments_);
                    regenerate(ctx, lastCommit_->bulge, segments_);
                    return;
                case VcbValue::Kind::CircleSegments: {
                    const double chordLen = geo::distance(lastCommit_->a, lastCommit_->b);
                    const double sweep = sweepForChordBulge(chordLen, std::fabs(lastCommit_->bulge));
                    segments_ = std::max(1, static_cast<int>(std::lround(value.count * sweep / kTwoPi)));
                    ctx.setSegments(segments_);
                    regenerate(ctx, lastCommit_->bulge, segments_);
                    return;
                }
                case VcbValue::Kind::Dims2:
                    ctx.setHint("Invalid entry.");
                    return;
            }
            return;
        }

        // Idle, nothing to retro-edit: Segments/CircleSegments still set segments_ for the next arc; other kinds are invalid.
        switch (value.kind) {
            case VcbValue::Kind::Segments:
            case VcbValue::Kind::CircleSegments:
                segments_ = std::clamp(value.count, kMinSegments, kMaxSegments);
                ctx.setSegments(segments_);
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

    if (!chordEnd_) {
        // Chord rubber-band stage: start_ placed, advancing to the chord end.
        switch (value.kind) {
            case VcbValue::Kind::Scalar: {
                if (!lastGround_ || std::fabs(value.a) <= geo::kMergeTol) {
                    ctx.setHint("Invalid entry.");
                    return;
                }
                const geo::Vec3 dir = geo::normalized(*lastGround_ - *start_);
                if (dir.x == 0.0 && dir.y == 0.0 && dir.z == 0.0) {
                    ctx.setHint("Invalid entry.");
                    return;
                }
                chordEnd_ = *start_ + dir * value.a;
                lastGround_ = chordEnd_;
                ctx.setPreview(previewVerts(*chordEnd_), *chordEnd_);
                ctx.setHint(kBulgeHint);
                updateVcb(ctx);
                return;
            }
            case VcbValue::Kind::Segments:
            case VcbValue::Kind::CircleSegments:
                segments_ = std::clamp(value.count, kMinSegments, kMaxSegments);
                ctx.setSegments(segments_);
                if (lastGround_) ctx.setPreview(previewVerts(*lastGround_), *lastGround_);
                updateVcb(ctx);
                return;
            case VcbValue::Kind::Radius:
            case VcbValue::Kind::Dims2:
                ctx.setHint("Invalid entry.");
                return;
        }
        return;
    }

    // Bulge stage: start_ and chordEnd_ both placed, nothing committed yet.
    switch (value.kind) {
        case VcbValue::Kind::Scalar: {
            if (!lastGround_) {
                ctx.setHint("Invalid entry.");
                return;
            }
            const double mag = std::fabs(value.a);
            if (mag <= geo::kMergeTol) {
                ctx.setHint("Invalid entry.");
                return;
            }
            const double cursorBulge = bulgeFor(*start_, *chordEnd_, *lastGround_);
            const double sign = cursorBulge < 0.0 ? -1.0 : 1.0;
            commitBulge(ctx, sign * mag);
            return;
        }
        case VcbValue::Kind::Radius: {
            const double chordLen = geo::distance(*start_, *chordEnd_);
            const std::optional<double> mag = sagittaForRadius(chordLen, value.a);
            if (!mag || *mag <= geo::kMergeTol) {
                ctx.setHint("Invalid entry.");
                return;
            }
            const double cursorBulge = lastGround_ ? bulgeFor(*start_, *chordEnd_, *lastGround_) : 0.0;
            const double sign = cursorBulge < 0.0 ? -1.0 : 1.0;
            commitBulge(ctx, sign * (*mag));
            return;
        }
        case VcbValue::Kind::Segments:
        case VcbValue::Kind::CircleSegments:
            segments_ = std::clamp(value.count, kMinSegments, kMaxSegments);
            ctx.setSegments(segments_);
            if (lastGround_) ctx.setPreview(previewVerts(*lastGround_), *lastGround_);
            updateVcb(ctx);
            return;
        case VcbValue::Kind::Dims2:
            ctx.setHint("Invalid entry.");
            return;
    }
}

void Arc2PointTool::onKeyDown(ToolContext& ctx, int key, bool ctrl) {
    if (key == Qt::Key_Escape) {
        lastCommit_.reset();
        reset(ctx);
        return;
    }

    // the reference modeler needs Ctrl for '+'/'-' segment resize; Key_Equal is '+' without shift.
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

    if (lastGround_) {
        ctx.setPreview(previewVerts(*lastGround_), *lastGround_);
    }
    updateVcb(ctx);
}

}  // namespace plnr::tools
