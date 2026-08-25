#include "arc3point_tool.h"

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

// Turns an open point chain into consecutive-pair line segments for
// ctx.setPreview -- unlike Circle/Polygon's loopVerts, this never closes back
// to the first point, matching every arc tool's open-polyline commit.
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

void Arc3PointTool::onActivate(ToolContext& ctx) {
    segments_ = geo::kArcDefaultSegments;
    lastCommit_.reset();
    reset(ctx);
}

void Arc3PointTool::onDeactivate(ToolContext& ctx) {
    start_.reset();
    pivot_.reset();
    lastGround_.reset();
    lastCommit_.reset();
    ctx.setPreview({}, std::nullopt);
}

geo::Inference Arc3PointTool::resolve(ToolContext& ctx, const PointerEvent& e) const {
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

std::vector<float> Arc3PointTool::previewVerts(const geo::Vec3& cursor) const {
    if (start_ && pivot_) {
        std::vector<geo::Vec3> arc = geo::arcPoints3Point(*start_, *pivot_, cursor, segments_);
        if (!arc.empty()) return chainVerts(arc);
        // Not yet a valid arc (collinear or coincident) -- fall back to the
        // straight path so the tool still gives feedback.
        return chainVerts({*start_, *pivot_, cursor});
    }
    if (start_) {
        return chainVerts({*start_, cursor});
    }
    return {};
}

void Arc3PointTool::reset(ToolContext& ctx) {
    start_.reset();
    pivot_.reset();
    lastGround_.reset();
    ctx.setPreview({}, std::nullopt);
    ctx.setHint(kActivationHint);
    updateVcb(ctx);
}

void Arc3PointTool::updateVcb(ToolContext& ctx) const {
    ctx.setVcbLabel("Length");
    if (!start_ || !lastGround_) return;
    const geo::Vec3& from = pivot_ ? *pivot_ : *start_;
    ctx.setVcbValue(formatApprox(geo::distance(from, *lastGround_)));
}

void Arc3PointTool::onPointerMove(ToolContext& ctx, const PointerEvent& e) {
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
    } else if (!pivot_) {
        ctx.setHint(kPivotHint);
    } else {
        ctx.setHint(kThirdHint);
    }
    updateVcb(ctx);
}

void Arc3PointTool::onPointerDown(ToolContext& ctx, const PointerEvent& e) {
    const geo::Inference inf = resolve(ctx, e);
    if (inf.kind == geo::InferenceKind::None) return;

    if (!start_) {
        start_ = inf.pos;
        lastGround_ = inf.pos;
        // Starting a new arc invalidates any prior retro-edit window -- a
        // follow-up typed segment-count must apply to THIS new arc once
        // it's committed, never reach back to the last one.
        lastCommit_.reset();
        return;
    }

    if (!pivot_) {
        if (geo::distance(inf.pos, *start_) < geo::kMergeTol) {
            // Degenerate pivot (same point re-clicked) -- ignore, stay armed
            // at start_, matching Circle/Polygon's own degenerate-click
            // guard.
            return;
        }
        pivot_ = inf.pos;
        lastGround_ = inf.pos;
        return;
    }

    const std::vector<geo::Vec3> points = geo::arcPoints3Point(*start_, *pivot_, inf.pos, segments_);
    if (points.empty()) {
        // Collinear triple (or a coincident pair) -- ignore the click,
        // stay armed at start_/pivot_.
        return;
    }

    ctx.requestAddPolyline(points, /*closed=*/false);
    lastCommit_ = LastCommit{*start_, *pivot_, inf.pos, segments_};
    reset(ctx);
}

void Arc3PointTool::onVcbCommit(ToolContext& ctx, const VcbValue& value) {
    if (!start_) {
        if (lastCommit_) {
            // Idle with an open retro-edit window: regenerate the just-
            // committed arc in place instead of starting a new one.
            switch (value.kind) {
                case VcbValue::Kind::Segments:
                case VcbValue::Kind::CircleSegments: {
                    segments_ = std::clamp(value.count, kMinSegments, kMaxSegments);
                    const std::vector<geo::Vec3> pts =
                        geo::arcPoints3Point(lastCommit_->p1, lastCommit_->p2, lastCommit_->p3, segments_);
                    if (pts.empty()) {
                        ctx.setHint("Invalid entry.");
                        return;
                    }
                    ctx.requestReplaceLastPolyline(pts, /*closed=*/false);
                    lastCommit_->segments = segments_;
                    return;
                }
                case VcbValue::Kind::Scalar:
                case VcbValue::Kind::Radius:
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

    // Pivot/end stages: start_ placed, whether or not pivot_ is too.
    switch (value.kind) {
        case VcbValue::Kind::Scalar:
            // the reference modeler accepts a typed distance here (extending along the
            // current segment); deferred past this MVP -- silent no-op,
            // not "Invalid entry." (not a user error, just unimplemented).
            return;
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

void Arc3PointTool::onKeyDown(ToolContext& ctx, int key, bool ctrl) {
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

    if (lastGround_) {
        ctx.setPreview(previewVerts(*lastGround_), *lastGround_);
    }
    updateVcb(ctx);
}

}  // namespace plnr::tools
