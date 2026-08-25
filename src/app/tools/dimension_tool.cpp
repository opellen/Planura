#include "dimension_tool.h"

#include <cmath>
#include <iomanip>
#include <sstream>
#include <string>
#include <utility>

#include <Qt>

#include <geo/model.h>

namespace plnr::tools {

namespace {

// 2-decimal exact (non-approximating) readout.
std::string formatExact(double value) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2) << value;
    return oss.str();
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

// Fixed world-unit tick half-length (not scaled to a constant screen-pixel
// size).
constexpr double kTickHalfLen = 0.1;

// Ray ∩ plane(point, normal); nullopt when parallel or behind the ray origin.
std::optional<geo::Vec3> rayPlaneIntersect(const geo::Ray& ray, const geo::Vec3& point, const geo::Vec3& normal) {
    const double denom = geo::dot(ray.dir, normal);
    if (std::fabs(denom) < geo::kEps) return std::nullopt;
    const double t = geo::dot(point - ray.origin, normal) / denom;
    if (t <= geo::kEps) return std::nullopt;
    return ray.origin + ray.dir * t;
}

}  // namespace

geo::Inference DimensionTool::resolve(ToolContext& ctx, const PointerEvent& e) const {
    const geo::Model* model = ctx.model();
    if (!model) return geo::Inference{};

    geo::InferenceContext ictx;
    ictx.tols = e.tols;
    ictx.guideLines = ctx.guideLines();
    ictx.guidePoints = ctx.guidePoints();
    return geo::infer(*model, e.ray, ictx);
}

std::optional<std::pair<geo::Id, geo::Vec3>> DimensionTool::nearestVertex(const geo::Model* model,
                                                                           const geo::Inference& inf,
                                                                           double vertexTol) const {
    if (!model || inf.kind == geo::InferenceKind::None) return std::nullopt;
    const geo::Vertex* v = model->findVertex(inf.pos, vertexTol);
    if (!v) return std::nullopt;
    return std::make_pair(v->id, v->pos);
}

geo::Vec3 DimensionTool::defaultPerpDir(const geo::Vec3& dir) const {
    const geo::Vec3 helper = std::fabs(dir.z) > 0.9 ? geo::Vec3{1.0, 0.0, 0.0} : geo::Vec3{0.0, 0.0, 1.0};
    const geo::Vec3 perp = geo::cross(dir, helper);
    const double len = geo::length(perp);
    return len > geo::kEps ? perp * (1.0 / len) : geo::Vec3{1.0, 0.0, 0.0};
}

std::string DimensionTool::currentHint() const {
    // UNVERIFIED -- no verbatim Dimension-tool hint text source; wording is
    // a reasonable placeholder.
    switch (stage_) {
        case Stage::PickA:
            return "Click near a model vertex to start the dimension.";
        case Stage::PickB:
            return "Click near a second model vertex to complete the dimension.";
        case Stage::PlaceOffset:
            return "Move to place the dimension line, then click.";
    }
    return {};
}

void DimensionTool::resetToIdle(ToolContext& ctx) {
    stage_ = Stage::PickA;
    vertexA_ = geo::kInvalidId;
    vertexB_ = geo::kInvalidId;
    hoverB_.reset();
    offsetDir_ = geo::Vec3{0.0, 0.0, 1.0};
    offset_ = 0.0;
    ctx.setPreview({}, std::nullopt);
    ctx.setInferenceCue(std::nullopt);
    ctx.setVcbLabel("Length");
    ctx.setHint(currentHint());
}

void DimensionTool::onActivate(ToolContext& ctx) {
    resetToIdle(ctx);
}

void DimensionTool::onDeactivate(ToolContext& ctx) {
    ctx.setPreview({}, std::nullopt);
    ctx.setInferenceCue(std::nullopt);
}

void DimensionTool::updateOffsetPreview(ToolContext& ctx, const PointerEvent& e) {
    const geo::Vec3 abDir = geo::normalized(posB_ - posA_);
    const geo::Vec3 mid = (posA_ + posB_) * 0.5;

    const std::optional<geo::Vec3> hit = rayPlaneIntersect(e.ray, mid, abDir);
    if (hit) {
        // *hit already lies in the plane through mid whose normal is abDir,
        // so (*hit - mid) is already perpendicular to AB by construction --
        // no separate projection step needed.
        const geo::Vec3 off = *hit - mid;
        const double len = geo::length(off);
        if (len > geo::kEps) {
            offsetDir_ = off * (1.0 / len);
            offset_ = len;
        }
        // else: degenerate (cursor projects onto the AB line itself) --
        // keep the previous offsetDir_/offset_.
    }
    // else: ray parallel to the plane -- keep the previous values too.

    const geo::Vec3 dimA = posA_ + offsetDir_ * offset_;
    const geo::Vec3 dimB = posB_ + offsetDir_ * offset_;
    // abDir and offsetDir_ are always mutually perpendicular unit vectors
    // (offsetDir_ is derived exactly as above), so their sum is never
    // degenerate -- a stable 45 degree tick direction with no extra guard.
    const geo::Vec3 tick = geo::normalized(abDir + offsetDir_) * kTickHalfLen;

    std::vector<float> verts;
    appendSegment(verts, posA_, dimA);
    appendSegment(verts, posB_, dimB);
    appendSegment(verts, dimA, dimB);
    appendSegment(verts, dimA - tick, dimA + tick);
    appendSegment(verts, dimB - tick, dimB + tick);
    ctx.setPreview(std::move(verts), std::nullopt);

    ctx.setVcbLabel("Length");
    ctx.setVcbValue(formatExact(geo::distance(posA_, posB_)));
    ctx.setHint(currentHint());
}

void DimensionTool::onPointerMove(ToolContext& ctx, const PointerEvent& e) {
    switch (stage_) {
        case Stage::PickA: {
            const geo::Inference inf = resolve(ctx, e);
            ctx.setInferenceCue(inf.kind != geo::InferenceKind::None ? cueFor(inf) : std::nullopt);
            const auto nv = nearestVertex(ctx.model(), inf, e.tols.vertexTol);
            ctx.setPreview({}, nv ? std::optional<geo::Vec3>(nv->second) : std::nullopt);
            ctx.setVcbLabel("Length");
            ctx.setHint(currentHint());
            return;
        }
        case Stage::PickB: {
            const geo::Inference inf = resolve(ctx, e);
            ctx.setInferenceCue(inf.kind != geo::InferenceKind::None ? cueFor(inf) : std::nullopt);
            const auto nv = nearestVertex(ctx.model(), inf, e.tols.vertexTol);
            if (nv && nv->first != vertexA_) hoverB_ = nv->second;
            // else: keep the previous hoverB_ (miss, or hovering vertexA_
            // itself) -- no-flicker convention.

            std::vector<float> verts;
            if (hoverB_) appendSegment(verts, posA_, *hoverB_);
            ctx.setPreview(std::move(verts), hoverB_);
            ctx.setVcbLabel("Length");
            if (hoverB_) ctx.setVcbValue(formatExact(geo::distance(posA_, *hoverB_)));
            ctx.setHint(currentHint());
            return;
        }
        case Stage::PlaceOffset:
            updateOffsetPreview(ctx, e);
            return;
    }
}

void DimensionTool::onPointerDown(ToolContext& ctx, const PointerEvent& e) {
    switch (stage_) {
        case Stage::PickA: {
            const geo::Inference inf = resolve(ctx, e);
            const auto nv = nearestVertex(ctx.model(), inf, e.tols.vertexTol);
            if (!nv) {
                ctx.setHint(currentHint());  // same instructional lead re-shown on a miss
                return;
            }
            vertexA_ = nv->first;
            posA_ = nv->second;
            stage_ = Stage::PickB;
            hoverB_.reset();
            ctx.setPreview({}, std::optional<geo::Vec3>(posA_));
            ctx.setHint(currentHint());
            return;
        }
        case Stage::PickB: {
            const geo::Inference inf = resolve(ctx, e);
            const auto nv = nearestVertex(ctx.model(), inf, e.tols.vertexTol);
            if (!nv || nv->first == vertexA_) {
                ctx.setHint(currentHint());  // miss, or same vertex as A -- stay in PickB
                return;
            }
            vertexB_ = nv->first;
            posB_ = nv->second;
            stage_ = Stage::PlaceOffset;
            offsetDir_ = defaultPerpDir(geo::normalized(posB_ - posA_));
            offset_ = 0.0;
            ctx.setHint(currentHint());
            return;
        }
        case Stage::PlaceOffset: {
            ctx.requestAddDimension(vertexA_, vertexB_, offsetDir_, offset_);
            resetToIdle(ctx);
            return;
        }
    }
}

void DimensionTool::onKeyDown(ToolContext& ctx, int key, bool /*ctrl*/) {
    if (key != Qt::Key_Escape) return;
    resetToIdle(ctx);
}

}  // namespace plnr::tools
