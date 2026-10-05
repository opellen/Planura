#include "rotated_rectangle_tool.h"

#include <cmath>
#include <cstddef>

#include <Qt>

namespace plnr::tools {

namespace {

// Closed point loop -> consecutive + closing segments for ctx.setPreview.
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

// Open point chain -> consecutive-pair segments for ctx.setPreview (straight baseline preview before the width stage).
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

// Ray/z=0 intersection (mirrors infer()'s GroundPlane rung), the fallback when a higher-priority hit won; nullopt if the ray misses z=0 ahead.
std::optional<geo::Vec3> groundPlaneHit(const geo::Ray& ray) {
    const double denom = ray.dir.z;
    if (std::fabs(denom) < geo::kEps) return std::nullopt;
    const double t = -ray.origin.z / denom;
    if (t <= geo::kEps) return std::nullopt;
    return ray.origin + ray.dir * t;
}

}  // namespace

void RotatedRectangleTool::onActivate(ToolContext& ctx) {
    reset(ctx);
}

void RotatedRectangleTool::onDeactivate(ToolContext& ctx) {
    a_.reset();
    b_.reset();
    ctx.setPreview({}, std::nullopt);
}

geo::Inference RotatedRectangleTool::resolve(ToolContext& ctx, const PointerEvent& e) const {
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

std::vector<geo::Vec3> RotatedRectangleTool::pointsFor(const geo::Vec3& a, const geo::Vec3& b,
                                                        const geo::Vec3& cursor) const {
    static constexpr geo::Vec3 kNormal{0.0, 0.0, 1.0};

    const geo::Vec3 ab = b - a;
    const double abLen = geo::length(ab);
    if (abLen <= geo::kMergeTol) return {};

    const geo::Vec3 abDir = ab * (1.0 / abLen);
    // Unit: kNormal and abDir are always perpendicular on the ground plane.
    const geo::Vec3 widthAxis = geo::cross(kNormal, abDir);

    const double widthLen = geo::dot(cursor - b, widthAxis);
    if (std::abs(widthLen) <= geo::kMergeTol) return {};

    const geo::Vec3 w = widthAxis * widthLen;
    return {a, b, b + w, a + w};
}

void RotatedRectangleTool::reset(ToolContext& ctx) {
    a_.reset();
    b_.reset();
    ctx.setPreview({}, std::nullopt);
    ctx.setHint(kActivationHint);
}

void RotatedRectangleTool::onPointerMove(ToolContext& ctx, const PointerEvent& e) {
    const geo::Inference inf = resolve(ctx, e);
    const bool hasTarget = inf.kind != geo::InferenceKind::None;

    std::optional<geo::Vec3> marker;
    if (hasTarget) marker = inf.pos;

    std::vector<float> lineVerts;
    if (hasTarget) {
        if (a_ && b_) {
            lineVerts = loopVerts(pointsFor(*a_, *b_, inf.pos));
        } else if (a_) {
            lineVerts = chainVerts({*a_, inf.pos});
        }
    }
    ctx.setPreview(std::move(lineVerts), marker);

    if (!a_) {
        ctx.setHint(kActivationHint);
    } else if (!b_) {
        ctx.setHint(kBaselineHint);
    } else {
        ctx.setHint(kWidthHint);
    }
}

void RotatedRectangleTool::onPointerDown(ToolContext& ctx, const PointerEvent& e) {
    const geo::Inference inf = resolve(ctx, e);
    if (inf.kind == geo::InferenceKind::None) return;

    if (!a_) {
        a_ = inf.pos;
        return;
    }

    if (!b_) {
        if (geo::distance(inf.pos, *a_) < geo::kMergeTol) {
            // Degenerate baseline (same point re-clicked): ignore, stay armed at a_.
            return;
        }
        b_ = inf.pos;
        return;
    }

    const std::vector<geo::Vec3> points = pointsFor(*a_, *b_, inf.pos);
    if (points.empty()) {
        // Degenerate width (perpendicular offset below merge tolerance): ignore, stay armed at the baseline.
        return;
    }

    ctx.requestAddPolyline(points, /*closed=*/true);
    reset(ctx);
}

void RotatedRectangleTool::onKeyDown(ToolContext& ctx, int key, bool /*ctrl*/) {
    if (key == Qt::Key_Escape) {
        reset(ctx);
    }
}

}  // namespace plnr::tools
