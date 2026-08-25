#include "flip_tool.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <utility>

#include <Qt>

#include <geo/model.h>

namespace plnr::tools {

namespace {

// Component accessor/index convention shared by axisHalfExtent/computePlane:
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

// A wireframe rectangle plus a diagonal cross -- stand-in for a translucent
// plane fill; the cross is what reads as "a plane" rather than an outline.
// Takes plain fields since a free function can't name FlipTool's nested type.
void appendPlaneWireframe(std::vector<float>& verts, const geo::Vec3& center, const geo::Vec3& axisU,
                           const geo::Vec3& axisV, double halfU, double halfV) {
    const geo::Vec3 uOff = axisU * halfU;
    const geo::Vec3 vOff = axisV * halfV;
    const geo::Vec3 c1 = center + uOff + vOff;
    const geo::Vec3 c2 = center - uOff + vOff;
    const geo::Vec3 c3 = center - uOff - vOff;
    const geo::Vec3 c4 = center + uOff - vOff;
    appendSegment(verts, c1, c2);
    appendSegment(verts, c2, c3);
    appendSegment(verts, c3, c4);
    appendSegment(verts, c4, c1);
    appendSegment(verts, c1, c3);  // diagonal cross
    appendSegment(verts, c2, c4);
}

// UNVERIFIED wording -- no confirmed real-the reference modeler Flip hint string exists.
constexpr const char* kIdleHint = "Click a plane to flip the selection. | Ctrl = Toggle Copy.";
constexpr const char* kEmptyHint = "Select something first.";

// axisIdx (0/1/2, or -1 for "nothing selected/hovered") -> the letter
// idleHintText's "Plane: " readout uses -- "none" for -1.
const char* planeLetter(int axisIdx) {
    switch (axisIdx) {
        case 0: return "X";
        case 1: return "Y";
        case 2: return "Z";
        default: return "none";
    }
}

// Past this many screen pixels of movement, a plane grab becomes a drag instead of a click.
constexpr qreal kDragThresholdPx = 5.0;

}  // namespace

void FlipTool::onActivate(ToolContext& ctx) {
    stage_ = Stage::Idle;
    bbox_ = Bbox{};
    offset_ = {0.0, 0.0, 0.0};
    selectedPlane_.reset();
    hoverPlane_ = -1;
    press_.reset();
    copyMode_ = false;

    if (refreshFromSelection(ctx)) {
        // A selection already exists at activation -- show the planes immediately.
        ctx.setPreviewBatches(buildPreview(), std::nullopt);
        ctx.setHint(idleHintText());
    }
}

void FlipTool::onDeactivate(ToolContext& ctx) {
    stage_ = Stage::Idle;
    bbox_ = Bbox{};
    offset_ = {0.0, 0.0, 0.0};
    selectedPlane_.reset();
    hoverPlane_ = -1;
    press_.reset();
    copyMode_ = false;
    ctx.setPreviewBatches({}, std::nullopt);
}

bool FlipTool::refreshFromSelection(ToolContext& ctx) {
    const geo::Model* model = ctx.model();
    const std::vector<events::EntityRef>& sel = ctx.selection();

    auto clearToIdle = [&]() {
        stage_ = Stage::Idle;
        bbox_ = Bbox{};
        offset_ = {0.0, 0.0, 0.0};
        hoverPlane_ = -1;
        press_.reset();
        ctx.setPreviewBatches({}, std::nullopt);
        ctx.setHint(kEmptyHint);
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
        // Defensive: shouldn't normally happen.
        clearToIdle();
        return false;
    }

    // Reset offset_ only on a real geometry/selection change, not on
    // ordinary hover (see class comment).
    const bool bboxChanged = !bbox_.valid || !geo::almostEqual(bbox_.min, mn, geo::kMergeTol) ||
                              !geo::almostEqual(bbox_.max, mx, geo::kMergeTol);
    bbox_ = Bbox{mn, mx, true};
    if (bboxChanged) offset_ = {0.0, 0.0, 0.0};
    stage_ = Stage::Armed;
    return true;
}

geo::Vec3 FlipTool::bboxCenter() const {
    return {(bbox_.min.x + bbox_.max.x) * 0.5, (bbox_.min.y + bbox_.max.y) * 0.5, (bbox_.min.z + bbox_.max.z) * 0.5};
}

double FlipTool::axisHalfExtent(int axis) const {
    return (axisComponent(bbox_.max, axis) - axisComponent(bbox_.min, axis)) * 0.5;
}

double FlipTool::margin() const {
    const double diag = geo::distance(bbox_.min, bbox_.max);
    // 20% of the bbox diagonal, floored at 0.3 world units so a degenerate
    // selection still gets a clickable plane.
    constexpr double kFraction = 0.2;
    constexpr double kMinMargin = 0.3;
    return std::max(diag * kFraction, kMinMargin);
}

FlipTool::PlaneGeom FlipTool::computePlane(int axisIdx) const {
    static constexpr geo::Vec3 kAxes[3] = {{1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {0.0, 0.0, 1.0}};
    static const PreviewColor kColors[3] = {kAxisRedColor, kAxisGreenColor, kAxisBlueColor};

    const int j = (axisIdx + 1) % 3;
    const int k = (axisIdx + 2) % 3;
    const double m = margin();

    PlaneGeom pg;
    pg.normal = kAxes[axisIdx];
    pg.axisU = kAxes[j];
    pg.axisV = kAxes[k];
    pg.center = bboxCenter() + kAxes[axisIdx] * offset_[static_cast<std::size_t>(axisIdx)];
    pg.halfU = axisHalfExtent(j) + m;
    pg.halfV = axisHalfExtent(k) + m;
    pg.color = kColors[axisIdx];
    return pg;
}

int FlipTool::pickPlane(const PointerEvent& e) const {
    if (!bbox_.valid) return -1;
    int best = -1;
    double bestT = std::numeric_limits<double>::max();
    for (int i = 0; i < 3; ++i) {
        const PlaneGeom pg = computePlane(i);
        const double denom = geo::dot(e.ray.dir, pg.normal);
        if (std::fabs(denom) < geo::kEps) continue;  // ray parallel to this plane
        const double t = geo::dot(pg.center - e.ray.origin, pg.normal) / denom;
        if (t <= geo::kEps) continue;  // behind the camera
        const geo::Vec3 p = e.ray.origin + e.ray.dir * t;
        const double u = geo::dot(p - pg.center, pg.axisU);
        const double v = geo::dot(p - pg.center, pg.axisV);
        if (std::fabs(u) > pg.halfU || std::fabs(v) > pg.halfV) continue;  // outside this plane's own rectangle
        // Nearest-hit tie-break for two visually overlapping planes near the
        // shared bbox center.
        if (t < bestT) {
            bestT = t;
            best = i;
        }
    }
    return best;
}

int FlipTool::activePlane() const {
    if (press_) return press_->plane;
    if (selectedPlane_) return *selectedPlane_;
    return hoverPlane_;
}

std::string FlipTool::idleHintText() const {
    // Two live observables appended so a scenario/agent can poll status().hint
    // to confirm the toggle/selection landed before sending a commit click.
    std::string hint(kIdleHint);
    hint += copyMode_ ? " (Copy: ON)" : " (Copy: OFF)";
    hint += " (Plane: ";
    hint += planeLetter(activePlane());
    hint += ")";
    return hint;
}

std::vector<ToolContext::PreviewBatch> FlipTool::buildPreview() const {
    std::vector<ToolContext::PreviewBatch> batches;
    if (!bbox_.valid) return batches;

    const int active = activePlane();
    for (int i = 0; i < 3; ++i) {
        const PlaneGeom pg = computePlane(i);
        std::vector<float> verts;
        appendPlaneWireframe(verts, pg.center, pg.axisU, pg.axisV, pg.halfU, pg.halfV);
        // Highlight is alpha-only (1.0 vs. 0.55) -- PreviewBatch carries no
        // line-width channel. Each plane keeps its own axis color as its own batch.
        const float alpha = (i == active) ? 1.0f : 0.55f;
        batches.push_back(ToolContext::PreviewBatch{std::move(verts), pg.color.r, pg.color.g, pg.color.b, alpha});
    }
    return batches;
}

void FlipTool::commitFlip(ToolContext& ctx, int planeIdx) {
    const std::vector<events::EntityRef>& sel = ctx.selection();
    if (sel.empty()) {
        // Selection vanished (e.g. externally cleared) -- nothing to flip.
        ctx.setHint(kEmptyHint);
        return;
    }

    const PlaneGeom pg = computePlane(planeIdx);

    events::TransformSpec spec;
    spec.kind = events::TransformSpec::Kind::Mirror;
    spec.planePoint = pg.center;
    spec.planeNormal = pg.normal;

    // copies=1 leaves the original and creates a mirrored copy (opens the
    // array retro-edit window); copies=0 mirrors in place. offset_ untouched.
    ctx.requestTransformEntities(sel, spec, copyMode_ ? 1 : 0);

    // Planes recompute lazily from the flipped selection's bbox on the next pointer event.
    ctx.setPreviewBatches(buildPreview(), std::nullopt);
    ctx.setHint(idleHintText());
}

void FlipTool::onPointerMove(ToolContext& ctx, const PointerEvent& e) {
    if (press_) {
        if (!press_->dragging) {
            const qreal dx = e.screen.x() - press_->pos.x();
            const qreal dy = e.screen.y() - press_->pos.y();
            if (std::sqrt(dx * dx + dy * dy) < kDragThresholdPx) return;  // still within click tolerance
            press_->dragging = true;
        }
        // Straight translate along the grabbed plane's own normal: the ray's
        // closest-approach parameter along that world-axis line through center.
        offset_[static_cast<std::size_t>(press_->plane)] =
            axisParameterForRay(e.ray, bboxCenter(), computePlane(press_->plane).normal);
        ctx.setPreviewBatches(buildPreview(), std::nullopt);
        ctx.setHint(idleHintText());
        return;
    }

    if (!refreshFromSelection(ctx)) return;  // hint/preview already set inside on failure

    hoverPlane_ = pickPlane(e);
    if (selectedPlane_ && hoverPlane_ >= 0 && hoverPlane_ != *selectedPlane_) {
        // Pointer moved onto a different plane than the arrow-key selection
        // -- hover takes back over.
        selectedPlane_.reset();
    }

    ctx.setPreviewBatches(buildPreview(), std::nullopt);
    ctx.setHint(idleHintText());
}

void FlipTool::onPointerDown(ToolContext& ctx, const PointerEvent& e) {
    if (press_) return;  // a press is already in progress -- shouldn't happen, ignore defensively

    if (!refreshFromSelection(ctx)) return;

    const int idx = pickPlane(e);
    if (idx < 0) return;  // miss -- ignore, stay Armed

    // A physical click supersedes whatever the arrow keys last selected.
    selectedPlane_.reset();
    hoverPlane_ = idx;

    PressState st;
    st.plane = idx;
    st.pos = e.screen;
    st.startOffset = offset_[static_cast<std::size_t>(idx)];
    st.dragging = false;
    press_ = st;

    ctx.setPreviewBatches(buildPreview(), std::nullopt);
}

void FlipTool::onPointerUp(ToolContext& ctx, const PointerEvent& e) {
    (void)e;
    if (!press_) return;
    const PressState st = *press_;
    press_.reset();

    if (st.dragging) {
        // Just a release -- the moved plane stays put; no flip. The NEXT
        // click flips about wherever it now sits.
        ctx.setPreviewBatches(buildPreview(), std::nullopt);
        ctx.setHint(idleHintText());
        return;
    }

    // No drag past threshold -- a plain click; commit the flip at the current position.
    commitFlip(ctx, st.plane);
}

void FlipTool::onKeyDown(ToolContext& ctx, int key, bool /*ctrl*/) {
    // Consumes the real Qt::Key_Control KeyPress (rising edge), not a
    // pointer-event modifier snapshot.
    if (key == Qt::Key_Control) {
        copyMode_ = !copyMode_;
        if (stage_ == Stage::Armed) {
            ctx.setPreviewBatches(buildPreview(), std::nullopt);
            ctx.setHint(idleHintText());
        }
        return;
    }

    if (key == Qt::Key_Escape) {
        // Cancels an in-progress drag and resets moved planes to bbox-centered;
        // a no-op if nothing moved. Does not touch copyMode_.
        const bool hadPress = press_.has_value();
        const bool hadOffset = offset_[0] != 0.0 || offset_[1] != 0.0 || offset_[2] != 0.0;
        press_.reset();
        if (hadOffset) offset_ = {0.0, 0.0, 0.0};
        if ((hadPress || hadOffset) && stage_ == Stage::Armed) {
            ctx.setPreviewBatches(buildPreview(), std::nullopt);
        }
        return;
    }

    if (stage_ != Stage::Armed) return;  // nothing to select/commit without planes on screen

    switch (key) {
        case Qt::Key_Left:
            selectedPlane_ = 1;  // green, Y-normal
            break;
        case Qt::Key_Right:
            selectedPlane_ = 0;  // red, X-normal
            break;
        case Qt::Key_Up:
            selectedPlane_ = 2;  // blue, Z-normal
            break;
        case Qt::Key_Return:
        case Qt::Key_Enter:
            // Commits a flip on the arrow-key-selected plane (MCP testing affordance).
            if (selectedPlane_) commitFlip(ctx, *selectedPlane_);
            return;
        default:
            return;
    }
    ctx.setPreviewBatches(buildPreview(), std::nullopt);
    // idleHintText()'s "(Plane: X/Y/Z)" readout confirms the selection landed before the commit key.
    ctx.setHint(idleHintText());
}

void FlipTool::onVcbCommit(ToolContext& ctx, const VcbValue& value) {
    // Only the post-copy array window is meaningful here; everything else is
    // ignored. requestApplyArrayTimes/Divide no-op silently if the window isn't open.
    switch (value.kind) {
        case VcbValue::Kind::ArrayTimes:
            if (value.count >= 1) ctx.requestApplyArrayTimes(value.count);
            return;
        case VcbValue::Kind::ArrayDivide:
            if (value.count >= 1) ctx.requestApplyArrayDivide(value.count);
            return;
        default:
            return;
    }
}

}  // namespace plnr::tools
