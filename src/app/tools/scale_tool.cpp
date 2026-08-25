#include "scale_tool.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <string>
#include <utility>

#include <Qt>

#include <geo/model.h>

namespace plnr::tools {

namespace {

// "~ " + value formatted to 2 decimals -- the VCB's approximate-readout convention.
std::string formatApprox(double value) {
    std::ostringstream oss;
    oss << "~ " << std::fixed << std::setprecision(2) << value;
    return oss.str();
}

// Component accessor/index convention shared by buildGrips/computeFactors:
// axis 0 = x, 1 = y, 2 = z.
double axisComponent(const geo::Vec3& v, int axis) {
    switch (axis) {
        case 0:
            return v.x;
        case 1:
            return v.y;
        default:
            return v.z;
    }
}

// Parameter s such that (anchor + s*axis) is the closest point on that
// line to ray; axis must be unit length.
double axisParameterForRay(const geo::Ray& ray, const geo::Vec3& anchor, const geo::Vec3& axis) {
    const double b = geo::dot(axis, ray.dir);
    const double denom = 1.0 - b * b;
    if (std::fabs(denom) < geo::kEps) return 0.0;
    const geo::Vec3 w0 = anchor - ray.origin;
    const double d = geo::dot(axis, w0);
    const double e = geo::dot(ray.dir, w0);
    return (b * e - d) / denom;
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

// The 12 edges of a box given its 8 corners in fixed order
// {(-,-,-),(+,-,-),(+,+,-),(-,+,-),(-,-,+),(+,-,+),(+,+,+),(-,+,+)}.
// Shared by appendBboxWireframe and the grip-cube builder below.
constexpr int kBoxEdges[12][2] = {
    {0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6}, {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7},
};

void appendBoxCorners(std::vector<float>& verts, const std::array<geo::Vec3, 8>& corners) {
    for (const auto& edge : kBoxEdges) appendSegment(verts, corners[edge[0]], corners[edge[1]]);
}

// Small axis-aligned wireframe cube at c with half-size h, what buildPreview
// draws each grip as; h stays constant world-size, only c is transformed.
void appendGripCube(std::vector<float>& verts, const geo::Vec3& c, double h) {
    const std::array<geo::Vec3, 8> corners{
        geo::Vec3{c.x - h, c.y - h, c.z - h}, geo::Vec3{c.x + h, c.y - h, c.z - h},
        geo::Vec3{c.x + h, c.y + h, c.z - h}, geo::Vec3{c.x - h, c.y + h, c.z - h},
        geo::Vec3{c.x - h, c.y - h, c.z + h}, geo::Vec3{c.x + h, c.y - h, c.z + h},
        geo::Vec3{c.x + h, c.y + h, c.z + h}, geo::Vec3{c.x - h, c.y + h, c.z + h},
    };
    appendBoxCorners(verts, corners);
}

}  // namespace

void ScaleTool::onActivate(ToolContext& ctx) {
    stage_ = Stage::Idle;
    bbox_ = Bbox{};
    grips_.clear();
    hoverGrip_ = -1;
    dragGrip_ = -1;
    uniformMode_ = true;
    aboutCenter_ = false;
    ctrlHeld_ = false;
    shiftHeld_ = false;
    lastCommit_.reset();

    ctx.setVcbLabel("Scale");
    if (refreshFromSelection(ctx)) {
        // A selection already exists at activation -- show its grips immediately.
        ctx.setPreviewBatches(buildPreview(ctx, Factors{}), std::nullopt);
        ctx.setHint("Click a scale grip to begin scaling. | Ctrl = Toggle Scale About Center. | Shift = Toggle Uniform Scale.");
    }
}

void ScaleTool::onDeactivate(ToolContext& ctx) {
    stage_ = Stage::Idle;
    bbox_ = Bbox{};
    grips_.clear();
    hoverGrip_ = -1;
    dragGrip_ = -1;
    aboutCenter_ = false;
    ctrlHeld_ = false;
    shiftHeld_ = false;
    lastCommit_.reset();
    ctx.setPreviewBatches({}, std::nullopt);
}

void ScaleTool::syncCtrl(bool ctrlNow) {
    if (ctrlNow && !ctrlHeld_) aboutCenter_ = !aboutCenter_;
    ctrlHeld_ = ctrlNow;
}

void ScaleTool::syncShift(bool shiftNow) {
    if (shiftNow && !shiftHeld_ && stage_ == Stage::Dragging) uniformMode_ = !uniformMode_;
    shiftHeld_ = shiftNow;
}

bool ScaleTool::refreshFromSelection(ToolContext& ctx) {
    const geo::Model* model = ctx.model();
    const std::vector<events::EntityRef>& sel = ctx.selection();

    auto clearToIdle = [&]() {
        stage_ = Stage::Idle;
        bbox_ = Bbox{};
        grips_.clear();
        hoverGrip_ = -1;
        ctx.setPreviewBatches({}, std::nullopt);
        ctx.setHint("Select something first.");
        updateVcb(ctx, nullptr);
    };

    if (!model || sel.empty()) {
        clearToIdle();
        return false;
    }

    std::vector<std::pair<geo::EntityKind, geo::Id>> seeds;
    seeds.reserve(sel.size());
    for (const events::EntityRef& ref : sel) seeds.emplace_back(ref.kind, ref.id);
    const geo::EntitySet closure = geo::closureOf(*model, seeds);

    geo::Vec3 mn{}, mx{};
    bool first = true;
    for (geo::Id vId : closure.vertices) {
        const geo::Vertex* v = model->vertex(vId);
        if (!v) continue;
        if (first) {
            mn = mx = v->pos;
            first = false;
        } else {
            mn.x = std::min(mn.x, v->pos.x);
            mn.y = std::min(mn.y, v->pos.y);
            mn.z = std::min(mn.z, v->pos.z);
            mx.x = std::max(mx.x, v->pos.x);
            mx.y = std::max(mx.y, v->pos.y);
            mx.z = std::max(mx.z, v->pos.z);
        }
    }
    if (first) {
        // Defensive: shouldn't normally happen, but there's nothing to scale either way.
        clearToIdle();
        return false;
    }

    bbox_ = Bbox{mn, mx, true};
    grips_ = buildGrips(bbox_);
    if (stage_ != Stage::Dragging) stage_ = Stage::GripIdle;
    return true;
}

std::vector<ScaleTool::Grip> ScaleTool::buildGrips(const Bbox& bbox) const {
    std::vector<Grip> grips;
    if (!bbox.valid) return grips;

    const geo::Vec3 center{(bbox.min.x + bbox.max.x) * 0.5, (bbox.min.y + bbox.max.y) * 0.5,
                            (bbox.min.z + bbox.max.z) * 0.5};

    // An axis is degenerate when the bbox has ~zero extent along it -- its
    // "extreme" collapses to center, and no grip may claim it as active.
    const std::array<bool, 3> degenerate{
        (bbox.max.x - bbox.min.x) <= geo::kMergeTol,
        (bbox.max.y - bbox.min.y) <= geo::kMergeTol,
        (bbox.max.z - bbox.min.z) <= geo::kMergeTol,
    };

    auto extreme = [&](int axis, int sign) -> double {
        if (degenerate[axis]) return axisComponent(center, axis);
        return sign > 0 ? axisComponent(bbox.max, axis) : axisComponent(bbox.min, axis);
    };
    // signs[i] in {-1, 0, +1}; 0 means "use the center coordinate" (an axis
    // this candidate never varies along).
    auto makePoint = [&](const std::array<int, 3>& signs) {
        geo::Vec3 p{};
        double* comps[3] = {&p.x, &p.y, &p.z};
        for (int i = 0; i < 3; ++i) {
            *comps[i] = signs[i] == 0 ? axisComponent(center, i) : extreme(i, signs[i]);
        }
        return p;
    };

    auto addCandidate = [&](std::array<int, 3> signs, std::array<bool, 3> axisActive) {
        int count = 0;
        for (int i = 0; i < 3; ++i) {
            if (axisActive[i] && degenerate[i]) axisActive[i] = false;  // drop a degenerate axis from the active set
            if (axisActive[i]) ++count;
        }
        if (count == 0) return;  // nothing left to scale -- e.g. a face grip whose only axis is degenerate

        const geo::Vec3 pos = makePoint(signs);

        // Opposite point per active axis only -- an axis this grip doesn't
        // scale keeps pos's own coordinate on the anchor too.
        std::array<int, 3> oppSigns = signs;
        for (int i = 0; i < 3; ++i) oppSigns[i] = axisActive[i] ? -signs[i] : signs[i];
        const geo::Vec3 anchor = makePoint(oppSigns);

        // Degenerate collapsing produces exact duplicates -- skip a candidate
        // landing on an already-added grip's position with the same axis set.
        for (const Grip& g : grips) {
            if (g.axis == axisActive && geo::almostEqual(g.pos, pos, geo::kMergeTol)) return;
        }
        grips.push_back(Grip{pos, anchor, axisActive, count});
    };

    // 8 corners: uniform (all 3 axes active).
    for (int sx : {-1, 1}) {
        for (int sy : {-1, 1}) {
            for (int sz : {-1, 1}) {
                addCandidate({sx, sy, sz}, {true, true, true});
            }
        }
    }

    // 6 face centers: single axis active (the face's own normal). A
    // single-degenerate-axis bbox always lands on 8 grips (no extra 9th grip).
    for (int axis = 0; axis < 3; ++axis) {
        for (int s : {-1, 1}) {
            std::array<int, 3> signs{0, 0, 0};
            signs[axis] = s;
            std::array<bool, 3> active{false, false, false};
            active[axis] = true;
            addCandidate(signs, active);
        }
    }

    // 12 edge midpoints: two axes active (every pair besides the axis the
    // edge itself runs along).
    constexpr int kPairs[3][2] = {{0, 1}, {0, 2}, {1, 2}};
    for (const auto& pr : kPairs) {
        for (int s0 : {-1, 1}) {
            for (int s1 : {-1, 1}) {
                std::array<int, 3> signs{0, 0, 0};
                signs[pr[0]] = s0;
                signs[pr[1]] = s1;
                std::array<bool, 3> active{false, false, false};
                active[pr[0]] = true;
                active[pr[1]] = true;
                addCandidate(signs, active);
            }
        }
    }

    return grips;
}

int ScaleTool::pickGrip(const PointerEvent& e) const {
    if (grips_.empty()) return -1;
    // *6: generous enough to hit a small grip cube; a judgment call, not a
    // verified the reference modeler size.
    const double tol = e.tols.vertexTol * 6.0;
    int best = -1;
    double bestDist = tol;
    for (std::size_t i = 0; i < grips_.size(); ++i) {
        const geo::Vec3 toGrip = grips_[i].pos - e.ray.origin;
        const double along = geo::dot(toGrip, e.ray.dir);
        if (along < 0.0) continue;  // grip is behind the camera
        const geo::Vec3 closest = e.ray.origin + e.ray.dir * along;
        const double d = geo::distance(closest, grips_[i].pos);
        if (d < bestDist) {
            bestDist = d;
            best = static_cast<int>(i);
        }
    }
    return best;
}

geo::Vec3 ScaleTool::bboxCenter() const {
    return {(bbox_.min.x + bbox_.max.x) * 0.5, (bbox_.min.y + bbox_.max.y) * 0.5, (bbox_.min.z + bbox_.max.z) * 0.5};
}

geo::Vec3 ScaleTool::currentAnchor() const {
    if (aboutCenter_) return bboxCenter();
    if (dragGrip_ >= 0 && static_cast<std::size_t>(dragGrip_) < grips_.size()) return grips_[static_cast<std::size_t>(dragGrip_)].anchor;
    return bboxCenter();
}

double ScaleTool::gripHalfSize() const {
    const double diag = geo::distance(bbox_.min, bbox_.max);
    constexpr double kFraction = 0.015;
    constexpr double kMin = 0.02;
    return std::max(diag * kFraction, kMin);
}

ScaleTool::Factors ScaleTool::computeFactors(const PointerEvent& e) const {
    Factors f;
    if (dragGrip_ < 0 || static_cast<std::size_t>(dragGrip_) >= grips_.size()) return f;
    const Grip& grip = grips_[static_cast<std::size_t>(dragGrip_)];
    const geo::Vec3 anchor = currentAnchor();

    if (uniformMode_) {
        // One factor shared by every active axis: the ray's closest-approach
        // parameter along the anchor->grip diagonal, as a fraction of that
        // diagonal's length (1.0 exactly at the grip's original position).
        const geo::Vec3 diag = grip.pos - anchor;
        const double diagLen = geo::length(diag);
        if (diagLen > geo::kEps) {
            const geo::Vec3 dir = diag * (1.0 / diagLen);
            const double s = axisParameterForRay(e.ray, anchor, dir);
            const double factor = s / diagLen;
            if (grip.axis[0]) f.fx = factor;
            if (grip.axis[1]) f.fy = factor;
            if (grip.axis[2]) f.fz = factor;
        }
        return f;
    }

    // Per-axis: one independent factor per active world axis, from the
    // ray's closest-approach parameter along that axis's own world line.
    static const geo::Vec3 kWorldAxes[3] = {{1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {0.0, 0.0, 1.0}};
    double* const factorSlots[3] = {&f.fx, &f.fy, &f.fz};
    for (int i = 0; i < 3; ++i) {
        if (!grip.axis[i]) continue;
        const double extent = axisComponent(grip.pos, i) - axisComponent(anchor, i);
        if (std::fabs(extent) <= geo::kEps) continue;  // shouldn't happen (active implies non-degenerate) -- guard anyway
        const double s = axisParameterForRay(e.ray, anchor, kWorldAxes[i]);
        *factorSlots[i] = s / extent;
    }
    return f;
}

void ScaleTool::appendBboxWireframe(std::vector<float>& verts, const geo::Transform& xf) const {
    const std::array<geo::Vec3, 8> corners{
        xf.apply({bbox_.min.x, bbox_.min.y, bbox_.min.z}), xf.apply({bbox_.max.x, bbox_.min.y, bbox_.min.z}),
        xf.apply({bbox_.max.x, bbox_.max.y, bbox_.min.z}), xf.apply({bbox_.min.x, bbox_.max.y, bbox_.min.z}),
        xf.apply({bbox_.min.x, bbox_.min.y, bbox_.max.z}), xf.apply({bbox_.max.x, bbox_.min.y, bbox_.max.z}),
        xf.apply({bbox_.max.x, bbox_.max.y, bbox_.max.z}), xf.apply({bbox_.min.x, bbox_.max.y, bbox_.max.z}),
    };
    appendBoxCorners(verts, corners);
}

void ScaleTool::appendSelectionTransformed(ToolContext& ctx, std::vector<float>& verts, const geo::Transform& xf) const {
    const geo::Model* model = ctx.model();
    if (!model) return;
    const std::vector<events::EntityRef>& sel = ctx.selection();
    if (sel.empty()) return;

    std::vector<std::pair<geo::EntityKind, geo::Id>> seeds;
    seeds.reserve(sel.size());
    for (const events::EntityRef& ref : sel) seeds.emplace_back(ref.kind, ref.id);
    const geo::EntitySet closure = geo::closureOf(*model, seeds);

    for (geo::Id edgeId : closure.edges) {
        const geo::Edge* edge = model->edge(edgeId);
        if (!edge) continue;
        const geo::HalfEdge* h0 = model->halfEdge(edge->halfEdges[0]);
        const geo::HalfEdge* h1 = model->halfEdge(edge->halfEdges[1]);
        if (!h0 || !h1) continue;
        const geo::Vertex* v0 = model->vertex(h0->origin);
        const geo::Vertex* v1 = model->vertex(h1->origin);
        if (!v0 || !v1) continue;
        appendSegment(verts, xf.apply(v0->pos), xf.apply(v1->pos));
    }
}

std::vector<ToolContext::PreviewBatch> ScaleTool::buildPreview(ToolContext& ctx, const Factors& f) const {
    std::vector<ToolContext::PreviewBatch> batches;
    if (!bbox_.valid) return batches;

    events::TransformSpec spec;
    spec.kind = events::TransformSpec::Kind::Scaling;
    spec.center = (dragGrip_ >= 0) ? currentAnchor() : bboxCenter();
    spec.fx = f.fx;
    spec.fy = f.fy;
    spec.fz = f.fz;
    // TransformSpec::at ignores its factor arg for Kind::Scaling; 1.0 just
    // materializes the spec as-is. Reused for bbox/grips/selection previews.
    const geo::Transform xf = spec.at(1.0);

    std::vector<float> bboxVerts;
    appendBboxWireframe(bboxVerts, xf);
    constexpr PreviewColor kBboxColor{0.95f, 0.85f, 0.10f, 1.0f};
    batches.push_back(ToolContext::PreviewBatch{std::move(bboxVerts), kBboxColor.r, kBboxColor.g, kBboxColor.b, kBboxColor.a});

    // Grip cubes: green by default. While Dragging, the grabbed grip plus
    // its anchor turn red (anchor need not coincide with any grip). While
    // hovering (GripIdle), just the hovered grip turns red.
    const double half = gripHalfSize();
    std::vector<float> greenVerts, redVerts;
    for (std::size_t i = 0; i < grips_.size(); ++i) {
        const geo::Vec3 pos = xf.apply(grips_[i].pos);
        const bool active = (stage_ == Stage::Dragging && static_cast<int>(i) == dragGrip_) ||
                             (stage_ != Stage::Dragging && static_cast<int>(i) == hoverGrip_);
        appendGripCube(active ? redVerts : greenVerts, pos, half);
    }
    if (stage_ == Stage::Dragging) {
        // xf.apply on the anchor is a numeric no-op (it's xf's own fixed
        // point); kept for consistency.
        appendGripCube(redVerts, xf.apply(currentAnchor()), half);
    }
    if (!greenVerts.empty()) {
        batches.push_back(
            ToolContext::PreviewBatch{std::move(greenVerts), kAxisGreenColor.r, kAxisGreenColor.g, kAxisGreenColor.b, kAxisGreenColor.a});
    }
    if (!redVerts.empty()) {
        batches.push_back(ToolContext::PreviewBatch{std::move(redVerts), kAxisRedColor.r, kAxisRedColor.g, kAxisRedColor.b, kAxisRedColor.a});
    }

    std::vector<float> selVerts;
    appendSelectionTransformed(ctx, selVerts, xf);
    if (!selVerts.empty()) {
        batches.push_back(ToolContext::PreviewBatch{std::move(selVerts), kDefaultPreviewColor.r, kDefaultPreviewColor.g,
                                                      kDefaultPreviewColor.b, kDefaultPreviewColor.a});
    }

    return batches;
}

void ScaleTool::updateVcb(ToolContext& ctx, const Factors* live) const {
    // Always "Scale" -- the reference modeler swaps in an axis label; not implemented here.
    ctx.setVcbLabel("Scale");
    if (!live || dragGrip_ < 0 || static_cast<std::size_t>(dragGrip_) >= grips_.size()) return;

    const Grip& grip = grips_[static_cast<std::size_t>(dragGrip_)];
    double shown = 1.0;
    if (uniformMode_) {
        // All active axes share the same factor by construction; any one is representative.
        if (grip.axis[0]) {
            shown = live->fx;
        } else if (grip.axis[1]) {
            shown = live->fy;
        } else {
            shown = live->fz;
        }
    } else {
        // Per-axis: shows only the dominant active axis' factor (furthest
        // from 1.0); commit/VCB-entry still supports true per-axis via Dims3.
        double best = 1.0;
        double bestDev = -1.0;
        const double* vals[3] = {&live->fx, &live->fy, &live->fz};
        for (int i = 0; i < 3; ++i) {
            if (!grip.axis[i]) continue;
            const double dev = std::fabs(*vals[i] - 1.0);
            if (dev > bestDev) {
                bestDev = dev;
                best = *vals[i];
            }
        }
        shown = best;
    }
    ctx.setVcbValue(formatApprox(shown));
}

void ScaleTool::commit(ToolContext& ctx, const Factors& f) {
    const Grip* grip = (dragGrip_ >= 0 && static_cast<std::size_t>(dragGrip_) < grips_.size())
                            ? &grips_[static_cast<std::size_t>(dragGrip_)]
                            : nullptr;
    const std::array<bool, 3> axisActive = grip ? grip->axis : std::array<bool, 3>{true, true, true};

    // A factor of exactly 0 on a touched axis is rejected (would collapse
    // geometry the kernel can't invert); negative is fine, it flips.
    if ((axisActive[0] && std::fabs(f.fx) <= geo::kEps) || (axisActive[1] && std::fabs(f.fy) <= geo::kEps) ||
        (axisActive[2] && std::fabs(f.fz) <= geo::kEps)) {
        ctx.showWarning("Invalid Scale!");
        return;
    }

    const std::vector<events::EntityRef>& sel = ctx.selection();
    if (sel.empty()) {
        // Selection vanished mid-drag -- nothing to scale.
        ctx.setHint("Select something first.");
        cancelDrag(ctx);
        return;
    }

    events::TransformSpec spec;
    spec.kind = events::TransformSpec::Kind::Scaling;
    spec.center = currentAnchor();
    spec.fx = f.fx;
    spec.fy = f.fy;
    spec.fz = f.fz;
    ctx.requestTransformEntities(sel, spec, /*copies=*/0);

    lastCommit_ = LastCommit{sel, spec.center, f.fx, f.fy, f.fz, axisActive};
    cancelDrag(ctx);
}

void ScaleTool::cancelDrag(ToolContext& ctx) {
    dragGrip_ = -1;
    hoverGrip_ = -1;
    stage_ = bbox_.valid ? Stage::GripIdle : Stage::Idle;
    if (stage_ == Stage::GripIdle) {
        ctx.setPreviewBatches(buildPreview(ctx, Factors{}), std::nullopt);
        ctx.setHint("Click a scale grip to begin scaling. | Ctrl = Toggle Scale About Center. | Shift = Toggle Uniform Scale.");
    } else {
        ctx.setPreviewBatches({}, std::nullopt);
        ctx.setHint("Select something first.");
    }
    updateVcb(ctx, nullptr);
}

void ScaleTool::onPointerMove(ToolContext& ctx, const PointerEvent& e) {
    syncCtrl(e.ctrl);
    syncShift(e.shift);

    if (stage_ != Stage::Dragging) {
        if (!refreshFromSelection(ctx)) return;  // hint/preview/VCB already set inside
        hoverGrip_ = pickGrip(e);
        ctx.setPreviewBatches(buildPreview(ctx, Factors{}), std::nullopt);
        ctx.setHint("Click a scale grip to begin scaling. | Ctrl = Toggle Scale About Center. | Shift = Toggle Uniform Scale.");
        updateVcb(ctx, nullptr);
        return;
    }

    const Factors f = computeFactors(e);
    ctx.setPreviewBatches(buildPreview(ctx, f), std::nullopt);
    ctx.setHint(
        "Click to finish scaling uniformly, or enter a scale factor or dimension. | Ctrl = Toggle Scale About Center. | "
        "Shift = Toggle Uniform Scale.");
    updateVcb(ctx, &f);
}

void ScaleTool::onPointerDown(ToolContext& ctx, const PointerEvent& e) {
    syncCtrl(e.ctrl);
    syncShift(e.shift);

    if (stage_ != Stage::Dragging) {
        if (!refreshFromSelection(ctx)) return;
        const int idx = pickGrip(e);
        if (idx < 0) {
            // Miss -- stay armed in GripIdle, ignore the click.
            hoverGrip_ = -1;
            ctx.setPreviewBatches(buildPreview(ctx, Factors{}), std::nullopt);
            return;
        }
        dragGrip_ = idx;
        hoverGrip_ = -1;
        uniformMode_ = grips_[static_cast<std::size_t>(idx)].activeCount == 3;
        stage_ = Stage::Dragging;
        ctx.setPreviewBatches(buildPreview(ctx, Factors{}), std::nullopt);
        ctx.setHint(
            "Click to finish scaling uniformly, or enter a scale factor or dimension. | Ctrl = Toggle Scale About Center. | "
            "Shift = Toggle Uniform Scale.");
        updateVcb(ctx, nullptr);  // factor unknown until the next pointer move
        return;
    }

    commit(ctx, computeFactors(e));
}

void ScaleTool::onKeyDown(ToolContext& ctx, int key, bool /*ctrl*/) {
    if (key != Qt::Key_Escape) return;
    if (stage_ == Stage::Dragging) cancelDrag(ctx);
    // Idle/GripIdle: no-op -- the tool stays armed.
}

void ScaleTool::onVcbCommit(ToolContext& ctx, const VcbValue& value) {
    if (stage_ == Stage::Dragging) {
        // A typed value finishes the current drag immediately.
        const Grip& grip = grips_[static_cast<std::size_t>(dragGrip_)];
        Factors f;
        switch (value.kind) {
            case VcbValue::Kind::Scalar:
                if (std::fabs(value.a) <= geo::kEps) {
                    ctx.showWarning("Invalid Scale!");
                    return;
                }
                f.fx = f.fy = f.fz = value.a;
                break;
            case VcbValue::Kind::Dims3:
                if (std::fabs(value.a) <= geo::kEps || std::fabs(value.b) <= geo::kEps || std::fabs(value.c) <= geo::kEps) {
                    ctx.showWarning("Invalid Scale!");
                    return;
                }
                f.fx = value.a;
                f.fy = value.b;
                f.fz = value.c;
                break;
            case VcbValue::Kind::Dims2: {
                if (grip.activeCount != 2 || std::fabs(value.a) <= geo::kEps || std::fabs(value.b) <= geo::kEps) {
                    ctx.showWarning("Invalid Scale!");
                    return;
                }
                // Apply a,b to the grip's two active axes in ascending x/y/z
                // order; the inactive third axis stays at factor 1.
                double* axisSlots[3] = {&f.fx, &f.fy, &f.fz};
                bool usedA = false;
                for (int i = 0; i < 3; ++i) {
                    if (!grip.axis[i]) continue;
                    if (!usedA) {
                        *axisSlots[i] = value.a;
                        usedA = true;
                    } else {
                        *axisSlots[i] = value.b;
                    }
                }
                break;
            }
            default:
                ctx.showWarning("Invalid Scale!");
                return;
        }
        commit(ctx, f);
        return;
    }

    // Not dragging: retype the just-committed scale. Only meaningful once
    // something has actually been committed this activation.
    if (!lastCommit_) {
        ctx.showWarning("Invalid Scale!");
        return;
    }

    // requestTransformEntities only applies forward (no undo snapshot), so
    // this sends a fresh DELTA scale about the same center: delta =
    // newAbs / oldAbs (valid since both scalings share one fixed point).
    Factors deltaF;
    switch (value.kind) {
        case VcbValue::Kind::Scalar:
            if (std::fabs(value.a) <= geo::kEps) {
                ctx.showWarning("Invalid Scale!");
                return;
            }
            deltaF.fx = value.a / lastCommit_->fx;
            deltaF.fy = value.a / lastCommit_->fy;
            deltaF.fz = value.a / lastCommit_->fz;
            break;
        case VcbValue::Kind::Dims3:
            if (std::fabs(value.a) <= geo::kEps || std::fabs(value.b) <= geo::kEps || std::fabs(value.c) <= geo::kEps) {
                ctx.showWarning("Invalid Scale!");
                return;
            }
            deltaF.fx = value.a / lastCommit_->fx;
            deltaF.fy = value.b / lastCommit_->fy;
            deltaF.fz = value.c / lastCommit_->fz;
            break;
        case VcbValue::Kind::Dims2: {
            // Valid only if the last commit's active-axis set was exactly 2 (an edge-grip drag).
            int activeCount = 0;
            for (bool a : lastCommit_->activeAxes) {
                if (a) ++activeCount;
            }
            if (activeCount != 2 || std::fabs(value.a) <= geo::kEps || std::fabs(value.b) <= geo::kEps) {
                ctx.showWarning("Invalid Scale!");
                return;
            }
            double newAbs[3] = {lastCommit_->fx, lastCommit_->fy, lastCommit_->fz};
            bool usedA = false;
            for (int i = 0; i < 3; ++i) {
                if (!lastCommit_->activeAxes[i]) continue;
                if (!usedA) {
                    newAbs[i] = value.a;
                    usedA = true;
                } else {
                    newAbs[i] = value.b;
                }
            }
            deltaF.fx = newAbs[0] / lastCommit_->fx;
            deltaF.fy = newAbs[1] / lastCommit_->fy;
            deltaF.fz = newAbs[2] / lastCommit_->fz;
            break;
        }
        default:
            ctx.showWarning("Invalid Scale!");
            return;
    }

    events::TransformSpec spec;
    spec.kind = events::TransformSpec::Kind::Scaling;
    spec.center = lastCommit_->center;
    spec.fx = deltaF.fx;
    spec.fy = deltaF.fy;
    spec.fz = deltaF.fz;
    ctx.requestTransformEntities(lastCommit_->refs, spec, /*copies=*/0);

    // Update the recorded ABSOLUTE factors so a further retype composes
    // correctly against this one.
    lastCommit_->fx *= deltaF.fx;
    lastCommit_->fy *= deltaF.fy;
    lastCommit_->fz *= deltaF.fz;
}

}  // namespace plnr::tools
