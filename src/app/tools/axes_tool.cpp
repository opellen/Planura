#include "axes_tool.h"

#include <algorithm>
#include <cmath>

#include <Qt>

namespace plnr::tools {

namespace {

// Preview line lengths/dash cadence: arbitrary; dashes are baked into vertex data.
constexpr double kAxesPreviewLen = 10.0;
constexpr double kAxesDashLen = 0.3;
constexpr double kAxesGapLen = 0.3;

void appendVertex(std::vector<float>& out, const geo::Vec3& p) {
    out.push_back(static_cast<float>(p.x));
    out.push_back(static_cast<float>(p.y));
    out.push_back(static_cast<float>(p.z));
}

void appendDashedSegment(std::vector<float>& out, const geo::Vec3& from, const geo::Vec3& to) {
    const geo::Vec3 dir = to - from;
    const double totalLen = geo::length(dir);
    if (totalLen < geo::kEps) return;
    const geo::Vec3 unit = dir * (1.0 / totalLen);
    for (double t = 0.0; t < totalLen; t += kAxesDashLen + kAxesGapLen) {
        const double segEnd = std::min(t + kAxesDashLen, totalLen);
        appendVertex(out, from + unit * t);
        appendVertex(out, from + unit * segEnd);
    }
}

// Cosmetic label/preview-color mapping for activeAxis_; does not determine the stored frame role.
const char* axisRoleName(AxesTool::ActiveAxis axis) {
    switch (axis) {
        case AxesTool::ActiveAxis::Red: return "Red";
        case AxesTool::ActiveAxis::Blue: return "Blue";
        case AxesTool::ActiveAxis::Green: return "Green";
    }
    return "Red";
}

PreviewColor axisRoleColor(AxesTool::ActiveAxis axis) {
    switch (axis) {
        case AxesTool::ActiveAxis::Red: return kAxisRedColor;
        case AxesTool::ActiveAxis::Blue: return kAxisBlueColor;
        case AxesTool::ActiveAxis::Green: return kAxisGreenColor;
    }
    return kAxisRedColor;
}

// Projects raw onto the plane perpendicular to unit `axis`, returns the unit result; arbitrary stable perpendicular if raw is nearly parallel.
geo::Vec3 projectPerp(const geo::Vec3& raw, const geo::Vec3& axis) {
    const geo::Vec3 perp = raw - axis * geo::dot(raw, axis);
    geo::Vec3 result = geo::normalized(perp);
    if (result.x == 0.0 && result.y == 0.0 && result.z == 0.0) {
        const geo::Vec3 fallbackHint =
            std::fabs(axis.z) < 0.9 ? geo::cross(axis, geo::Vec3{0.0, 0.0, 1.0}) : geo::cross(axis, geo::Vec3{1.0, 0.0, 0.0});
        result = geo::normalized(fallbackHint);
    }
    return result;
}

}  // namespace

geo::Inference AxesTool::resolve(ToolContext& ctx, const PointerEvent& e) const {
    const geo::Model* model = ctx.model();
    if (!model) return geo::Inference{};

    geo::InferenceContext ictx;
    ictx.tols = e.tols;
    ictx.guideLines = ctx.guideLines();
    ictx.guidePoints = ctx.guidePoints();
    // anchor/chargedAnchors/referenceEdge/axisLock left unset: the perpendicular constraint is local.
    return geo::infer(*model, e.ray, ictx);
}

void AxesTool::cycleActiveAxis() {
    switch (activeAxis_) {
        case ActiveAxis::Red: activeAxis_ = ActiveAxis::Blue; break;
        case ActiveAxis::Blue: activeAxis_ = ActiveAxis::Green; break;
        case ActiveAxis::Green: activeAxis_ = ActiveAxis::Red; break;
    }
}

void AxesTool::resetToIdle(ToolContext& ctx) {
    stage_ = Stage::Idle;
    activeAxis_ = ActiveAxis::Red;
    ctx.setPreview({}, std::nullopt);
    ctx.setInferenceCue(std::nullopt);
    ctx.setHint(currentHint());
}

std::string AxesTool::currentHint() const {
    switch (stage_) {
        case Stage::Idle:
            return "Click to place the axes origin, or double-click to relocate.";
        case Stage::OriginSet: {
            // UNVERIFIED wording (no screenshot source), naming the active axis role.
            const std::string role = axisRoleName(activeAxis_);
            return "Click to set the " + role + " axis direction. | Alt = Toggle Active Axis (" + role + ").";
        }
        case Stage::FirstAxisSet:
            // UNVERIFIED wording (no screenshot source).
            return "Click to set the second axis direction, perpendicular to the first.";
    }
    return std::string();
}

void AxesTool::onActivate(ToolContext& ctx) {
    stage_ = Stage::Idle;
    activeAxis_ = ActiveAxis::Red;
    ctx.setHint(currentHint());
}

void AxesTool::onDeactivate(ToolContext& ctx) {
    stage_ = Stage::Idle;
    activeAxis_ = ActiveAxis::Red;
    ctx.setPreview({}, std::nullopt);
    ctx.setInferenceCue(std::nullopt);
}

void AxesTool::onPointerMove(ToolContext& ctx, const PointerEvent& e) {
    switch (stage_) {
        case Stage::Idle: {
            const geo::Inference inf = resolve(ctx, e);
            ctx.setPreview({}, std::nullopt);
            ctx.setInferenceCue(cueFor(inf));
            break;
        }
        case Stage::OriginSet: {
            const geo::Inference inf = resolve(ctx, e);
            ctx.setInferenceCue(std::nullopt);
            if (inf.kind == geo::InferenceKind::None) {
                ctx.setPreview({}, std::nullopt);
                break;
            }
            const geo::Vec3 dir = geo::normalized(inf.pos - origin_);
            if (dir.x == 0.0 && dir.y == 0.0 && dir.z == 0.0) {
                ctx.setPreview({}, std::nullopt);
                break;
            }
            std::vector<float> verts;
            appendDashedSegment(verts, origin_, origin_ + dir * kAxesPreviewLen);
            const PreviewColor color = axisRoleColor(activeAxis_);
            std::vector<ToolContext::PreviewBatch> batches;
            batches.push_back(ToolContext::PreviewBatch{std::move(verts), color.r, color.g, color.b, color.a});
            ctx.setPreviewBatches(std::move(batches), std::nullopt);
            break;
        }
        case Stage::FirstAxisSet: {
            const geo::Inference inf = resolve(ctx, e);
            ctx.setInferenceCue(std::nullopt);

            std::vector<ToolContext::PreviewBatch> batches;
            // The fixed first axis, shown in its (cosmetic) role color while aiming the second click.
            {
                std::vector<float> verts;
                appendDashedSegment(verts, origin_, origin_ + dir1_ * kAxesPreviewLen);
                const PreviewColor color = axisRoleColor(activeAxis_);
                batches.push_back(ToolContext::PreviewBatch{std::move(verts), color.r, color.g, color.b, color.a});
            }
            if (inf.kind != geo::InferenceKind::None) {
                const geo::Vec3 raw = inf.pos - origin_;
                const geo::Vec3 perp = projectPerp(raw, dir1_);
                // Follows the cursor's signed reach along perp (flips across the first axis); fixed length near zero.
                double len = geo::dot(raw, perp);
                if (std::fabs(len) < geo::kEps) len = kAxesPreviewLen;
                std::vector<float> verts;
                appendDashedSegment(verts, origin_, origin_ + perp * len);
                batches.push_back(ToolContext::PreviewBatch{std::move(verts), kDefaultPreviewColor.r,
                                                              kDefaultPreviewColor.g, kDefaultPreviewColor.b,
                                                              kDefaultPreviewColor.a});
            }
            ctx.setPreviewBatches(std::move(batches), std::nullopt);
            break;
        }
    }
    ctx.setHint(currentHint());
}

void AxesTool::onPointerDown(ToolContext& ctx, const PointerEvent& e) {
    switch (stage_) {
        case Stage::Idle: {
            const geo::Inference inf = resolve(ctx, e);
            if (inf.kind == geo::InferenceKind::None) return;
            origin_ = inf.pos;
            stage_ = Stage::OriginSet;
            activeAxis_ = ActiveAxis::Red;  // "first axis = RED by default" -- reset per new origin
            ctx.setHint(currentHint());
            break;
        }
        case Stage::OriginSet: {
            // A rapid second press is double-click-to-relocate (it lands here, not Idle): re-sends the current xDir/yDir to move origin_ only.
            if (e.clickCount >= 2) {
                const AxesFrame current = ctx.axesFrame();
                ctx.requestSetAxes(origin_, current.xDir, current.yDir);
                resetToIdle(ctx);
                return;
            }

            const geo::Inference inf = resolve(ctx, e);
            if (inf.kind == geo::InferenceKind::None) return;
            const geo::Vec3 dir = geo::normalized(inf.pos - origin_);
            if (dir.x == 0.0 && dir.y == 0.0 && dir.z == 0.0) return;  // degenerate -- stay in OriginSet
            dir1_ = dir;
            stage_ = Stage::FirstAxisSet;
            ctx.setHint(currentHint());
            break;
        }
        case Stage::FirstAxisSet: {
            const geo::Inference inf = resolve(ctx, e);
            if (inf.kind == geo::InferenceKind::None) return;
            const geo::Vec3 raw = inf.pos - origin_;
            const geo::Vec3 perp = projectPerp(raw, dir1_);
            ctx.requestSetAxes(origin_, dir1_, perp);
            resetToIdle(ctx);
            break;
        }
    }
}

void AxesTool::onKeyDown(ToolContext& ctx, int key, bool /*ctrl*/) {
    if (key == Qt::Key_Escape) {
        resetToIdle(ctx);
        return;
    }
    if (key == Qt::Key_Alt && stage_ == Stage::OriginSet) {
        cycleActiveAxis();
        ctx.setHint(currentHint());
    }
}

}  // namespace plnr::tools
