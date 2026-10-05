#include "freehand_tool.h"

#include <cmath>
#include <cstddef>

#include <Qt>

namespace plnr::tools {

namespace {

// Open point chain -> consecutive-pair segments for ctx.setPreview; never closes.
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

void FreehandTool::onActivate(ToolContext& ctx) {
    reset(ctx);
}

void FreehandTool::onDeactivate(ToolContext& ctx) {
    drawing_ = false;
    points_.clear();
    ctx.setPreview({}, std::nullopt);
}

geo::Inference FreehandTool::resolve(ToolContext& ctx, const PointerEvent& e) const {
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

void FreehandTool::reset(ToolContext& ctx) {
    drawing_ = false;
    points_.clear();
    ctx.setPreview({}, std::nullopt);
    ctx.setHint(kActivationHint);
}

void FreehandTool::onPointerDown(ToolContext& ctx, const PointerEvent& e) {
    const geo::Inference inf = resolve(ctx, e);
    if (inf.kind == geo::InferenceKind::None) return;

    drawing_ = true;
    points_.clear();
    points_.push_back(inf.pos);
    lastScreen_ = e.screen;
    ctx.setPreview(chainVerts(points_), inf.pos);
    ctx.setHint(kDrawingHint);
}

void FreehandTool::onPointerMove(ToolContext& ctx, const PointerEvent& e) {
    if (!drawing_) return;

    const geo::Inference inf = resolve(ctx, e);
    if (inf.kind == geo::InferenceKind::None) return;

    // Thinning: record only once the cursor moved far enough on screen (e.screen pixels, not world distance).
    const double dx = e.screen.x() - lastScreen_.x();
    const double dy = e.screen.y() - lastScreen_.y();
    if (std::sqrt(dx * dx + dy * dy) < kThinningPx) return;

    points_.push_back(inf.pos);
    lastScreen_ = e.screen;
    ctx.setPreview(chainVerts(points_), inf.pos);
}

void FreehandTool::onPointerUp(ToolContext& ctx, const PointerEvent& e) {
    if (!drawing_) return;

    if (points_.size() >= 2) {
        ctx.requestAddPolyline(points_, /*closed=*/false);
    }
    reset(ctx);
}

void FreehandTool::onKeyDown(ToolContext& ctx, int key, bool /*ctrl*/) {
    if (key == Qt::Key_Escape) {
        reset(ctx);
    }
}

}  // namespace plnr::tools
