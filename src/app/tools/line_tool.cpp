#include "line_tool.h"

#include <cmath>
#include <cstddef>

#include <Qt>

namespace plnr::tools {

namespace {

// Hover-charge threshold (UNVERIFIED constant) -- see updateHoverCharge()
// for the MVP timing approximation this drives.
constexpr qint64 kHoverChargeMs = 750;
constexpr std::size_t kMaxChargedAnchors = 3;

// Picks the axes-frame direction closest to delta (largest |dot|) --
// the reference modeler's shift-lock rule. Falls back to frame.xDir for a zero delta.
geo::Vec3 dominantAxis(const geo::Vec3& delta, const tools::AxesFrame& frame) {
    const double ax = std::fabs(geo::dot(delta, frame.xDir));
    const double ay = std::fabs(geo::dot(delta, frame.yDir));
    const double az = std::fabs(geo::dot(delta, frame.zDir));
    if (ax >= ay && ax >= az) return frame.xDir;
    if (ay >= az) return frame.yDir;
    return frame.zDir;
}

// Classifies dir into the frame's OWN Red/Green/Blue role -- unlike tool.h's
// WORLD-space axisColorFor, stays correct once the frame is relocated/rotated.
int frameAxisRole(const geo::Vec3& dir, const tools::AxesFrame& frame) {
    const double dx = std::fabs(geo::dot(dir, frame.xDir));
    const double dy = std::fabs(geo::dot(dir, frame.yDir));
    const double dz = std::fabs(geo::dot(dir, frame.zDir));
    if (dx >= dy && dx >= dz) return 0;
    if (dy >= dz) return 1;
    return 2;
}

PreviewColor frameAxisColorFor(const geo::Vec3& dir, const tools::AxesFrame& frame) {
    switch (frameAxisRole(dir, frame)) {
        case 0: return kAxisRedColor;
        case 1: return kAxisGreenColor;
        default: return kAxisBlueColor;
    }
}

const char* frameAxisTipSuffixFor(const geo::Vec3& dir, const tools::AxesFrame& frame) {
    switch (frameAxisRole(dir, frame)) {
        case 0: return "Red Axis";
        case 1: return "Green Axis";
        default: return "Blue Axis";
    }
}

// InferenceKinds the Alt cycle's AllOff/ParallelPerpendicularOnly states can
// suppress -- infer.h's "line-cue" grouping, minus OnEdge/GuideLine (not wired in yet).
bool isAxisLineCue(geo::InferenceKind k) {
    return k == geo::InferenceKind::OnAxis || k == geo::InferenceKind::FromPoint;
}
bool isReferenceLineCue(geo::InferenceKind k) {
    return k == geo::InferenceKind::Parallel || k == geo::InferenceKind::Perpendicular;
}

bool isChargeablePointKind(geo::InferenceKind k) {
    return k == geo::InferenceKind::Endpoint || k == geo::InferenceKind::Midpoint ||
           k == geo::InferenceKind::Intersection;
}

}  // namespace

void LineTool::onActivate(ToolContext& ctx) {
    anchor_.reset();
    lastGround_.reset();
    chargedAnchors_.clear();
    referenceEdge_.reset();
    hoverPoint_.reset();
    hoverKind_ = geo::InferenceKind::None;
    hoverCharged_ = false;
    axisLock_.reset();
    lockedArrowKey_ = 0;
    lastRay_.reset();
    linearMode_ = LinearInferenceMode::AllOn;
    ctx.setHint(currentHint());
}

void LineTool::onDeactivate(ToolContext& ctx) {
    anchor_.reset();
    lastGround_.reset();
    chargedAnchors_.clear();
    referenceEdge_.reset();
    hoverPoint_.reset();
    hoverCharged_ = false;
    axisLock_.reset();
    lockedArrowKey_ = 0;
    lastRay_.reset();
    ctx.setPreview({}, std::nullopt);
    ctx.setInferenceCue(std::nullopt);
}

geo::Inference LineTool::filterInference(geo::Inference inf) const {
    // Filters the RESOLVED result rather than reshaping InferenceContext --
    // a suppressed kind reverts to None here; point inferences unaffected.
    switch (linearMode_) {
        case LinearInferenceMode::AllOn:
            return inf;
        case LinearInferenceMode::AllOff:
            if (isAxisLineCue(inf.kind) || isReferenceLineCue(inf.kind)) return geo::Inference{};
            return inf;
        case LinearInferenceMode::ParallelPerpendicularOnly:
            if (isAxisLineCue(inf.kind)) return geo::Inference{};
            return inf;
    }
    return inf;
}

geo::Inference LineTool::resolve(ToolContext& ctx, const PointerEvent& e) const {
    const geo::Model* model = ctx.model();
    if (!model) return geo::Inference{};

    const AxesFrame frame = ctx.axesFrame();

    geo::InferenceContext base;
    base.anchor = anchor_;
    base.tols = e.tols;
    base.chargedAnchors = chargedAnchors_;
    base.referenceEdge = referenceEdge_;
    // FromPoint tests the current axes frame's own directions, not
    // hardcoded world X/Y/Z.
    base.axisDirs = {frame.xDir, frame.yDir, frame.zDir};

    if (axisLock_) {
        // Explicit arrow-key lock takes priority over Shift's transient
        // dominant-axis computation below (a stronger, deliberate action).
        base.axisLock = axisLock_;
        return filterInference(geo::infer(*model, e.ray, base));
    }

    if (!anchor_ || !e.shift) {
        return filterInference(geo::infer(*model, e.ray, base));
    }

    // Shift-locked: infer once unlocked to find where the pointer aims,
    // derive the dominant axis, then re-infer locked onto that line.
    const geo::Inference unlocked = geo::infer(*model, e.ray, base);
    base.axisLock = geo::AxisLock{*anchor_, dominantAxis(unlocked.pos - *anchor_, frame)};
    return filterInference(geo::infer(*model, e.ray, base));
}

void LineTool::updateHoverCharge(const geo::Inference& inf) {
    if (!isChargeablePointKind(inf.kind)) {
        hoverPoint_.reset();
        hoverKind_ = geo::InferenceKind::None;
        hoverCharged_ = false;
        return;
    }

    const bool samePoint = hoverPoint_ && hoverKind_ == inf.kind && geo::almostEqual(*hoverPoint_, inf.pos);
    if (!samePoint) {
        hoverPoint_ = inf.pos;
        hoverKind_ = inf.kind;
        hoverCharged_ = false;
        hoverTimer_.start();
        return;
    }

    if (!hoverCharged_ && hoverTimer_.isValid() && hoverTimer_.elapsed() >= kHoverChargeMs) {
        addChargedAnchor(inf.pos);
        hoverCharged_ = true;
    }
}

void LineTool::addChargedAnchor(const geo::Vec3& pos) {
    for (const geo::Vec3& a : chargedAnchors_) {
        if (geo::almostEqual(a, pos)) return;  // already charged -- no duplicate FIFO entries
    }
    chargedAnchors_.push_back(pos);
    if (chargedAnchors_.size() > kMaxChargedAnchors) {
        chargedAnchors_.erase(chargedAnchors_.begin());  // FIFO cap
    }
}

std::string LineTool::currentHint() const {
    std::string state;
    switch (linearMode_) {
        case LinearInferenceMode::AllOn: state = "All On"; break;
        case LinearInferenceMode::AllOff: state = "All Off"; break;
        case LinearInferenceMode::ParallelPerpendicularOnly: state = "Parallel and Perpendicular Only"; break;
    }

    std::string lead = anchor_ ? "Click to set second endpoint or enter length." : "Select start point.";
    return lead + " | Alt = Toggle Linear Inferences (" + state + "). | Arrow Keys = Toggle Lock Inference Direction.";
}

void LineTool::onPointerMove(ToolContext& ctx, const PointerEvent& e) {
    lastRay_ = e.ray;
    lastTols_ = e.tols;

    const AxesFrame frame = ctx.axesFrame();
    const geo::Inference inf = resolve(ctx, e);
    updateHoverCharge(inf);

    const bool hasTarget = inf.kind != geo::InferenceKind::None;
    if (hasTarget) lastGround_ = inf.pos;

    // Rubber-band line color: axis-colored when explicitly locked or the
    // inference is OnAxis/FromPoint; default otherwise (e.g. Parallel/Perpendicular).
    PreviewColor lineColor = kDefaultPreviewColor;
    if (axisLock_) {
        lineColor = frameAxisColorFor(axisLock_->dir, frame);
    } else if ((inf.kind == geo::InferenceKind::OnAxis || inf.kind == geo::InferenceKind::FromPoint) && inf.dir) {
        lineColor = frameAxisColorFor(*inf.dir, frame);
    }

    std::vector<ToolContext::PreviewBatch> batches;
    if (anchor_ && hasTarget) {
        std::vector<float> lineVerts = {
            static_cast<float>(anchor_->x), static_cast<float>(anchor_->y), static_cast<float>(anchor_->z),
            static_cast<float>(inf.pos.x),  static_cast<float>(inf.pos.y),  static_cast<float>(inf.pos.z),
        };
        batches.push_back(
            ToolContext::PreviewBatch{std::move(lineVerts), lineColor.r, lineColor.g, lineColor.b, lineColor.a});
    }
    // No legacy marker (nullopt) -- setInferenceCue below owns marker
    // rendering, a second dot here would double up with it.
    ctx.setPreviewBatches(std::move(batches), std::nullopt);

    if (!hasTarget) {
        ctx.setInferenceCue(std::nullopt);
    } else if (axisLock_) {
        // Locked rendering (verified): a RED SQUARE marker at the locked
        // endpoint plus a dotted trace to the raw cursor (no "Bold" width).
        InferenceCue lockCue;
        lockCue.pos = inf.pos;
        lockCue.shape = kMarkerSquare;
        lockCue.color = kLockMarkerColor;
        lockCue.screenTip = std::string("On ") + frameAxisTipSuffixFor(axisLock_->dir, frame);

        const geo::Model* model = ctx.model();
        if (model) {
            geo::InferenceContext unlockedCtx;
            unlockedCtx.anchor = anchor_;
            unlockedCtx.tols = e.tols;
            unlockedCtx.chargedAnchors = chargedAnchors_;
            unlockedCtx.referenceEdge = referenceEdge_;
            const geo::Inference unlocked = geo::infer(*model, e.ray, unlockedCtx);
            if (unlocked.kind != geo::InferenceKind::None) {
                lockCue.traceFrom = unlocked.pos;
                lockCue.traceColor = frameAxisColorFor(axisLock_->dir, frame);
            }
        }
        ctx.setInferenceCue(lockCue);
    } else {
        ctx.setInferenceCue(cueFor(inf));
    }

    ctx.setHint(currentHint());
}

void LineTool::onPointerDown(ToolContext& ctx, const PointerEvent& e) {
    const geo::Inference inf = resolve(ctx, e);
    if (inf.kind == geo::InferenceKind::None) return;

    if (!anchor_) {
        anchor_ = inf.pos;
        return;
    }

    const geo::Model* model = ctx.model();
    const std::size_t facesBefore = model ? model->faces().size() : 0;

    ctx.requestAddEdge(*anchor_, inf.pos);

    // requestAddEdge dispatches synchronously, so by the time it returns,
    // model already reflects the commit.
    const bool chainClosed = model && model->faces().size() > facesBefore;
    if (chainClosed) {
        anchor_.reset();
        ctx.setHint(currentHint());
    } else {
        anchor_ = inf.pos;
    }
}

void LineTool::onVcbCommit(ToolContext& ctx, const VcbValue& value) {
    if (!anchor_ || !lastGround_ || value.kind != VcbValue::Kind::Scalar) {
        ctx.setHint("Invalid entry.");
        return;
    }
    if (std::fabs(value.a) <= geo::kMergeTol) {
        ctx.setHint("Invalid entry.");
        return;
    }
    const geo::Vec3 dir = geo::normalized(*lastGround_ - *anchor_);
    if (dir.x == 0.0 && dir.y == 0.0 && dir.z == 0.0) {
        ctx.setHint("Invalid entry.");
        return;
    }
    const geo::Vec3 endpoint = *anchor_ + dir * value.a;

    const geo::Model* model = ctx.model();
    const std::size_t facesBefore = model ? model->faces().size() : 0;

    // Same ctx.requestAddEdge call onPointerDown's own click-commit uses --
    // only the endpoint's source differs (computed here, picked there).
    ctx.requestAddEdge(*anchor_, endpoint);

    const bool chainClosed = model && model->faces().size() > facesBefore;
    if (chainClosed) {
        anchor_.reset();
        lastGround_.reset();
        ctx.setHint(currentHint());
    } else {
        anchor_ = endpoint;
        lastGround_ = endpoint;
    }
}

void LineTool::handleArrowLock(ToolContext& ctx, int key) {
    // Verified mapping (same as Section Plane's): Up=Blue, Right=Red,
    // Left=Green. Same arrow unlocks; a different one switches, not stacks.
    if (lockedArrowKey_ == key) {
        axisLock_.reset();
        lockedArrowKey_ = 0;
        return;
    }

    const AxesFrame frame = ctx.axesFrame();
    geo::Vec3 dir;
    if (key == Qt::Key_Up) {
        dir = frame.zDir;  // Blue = Z
    } else if (key == Qt::Key_Right) {
        dir = frame.xDir;  // Red = X
    } else {
        dir = frame.yDir;  // Left = Green = Y
    }

    // The lock's origin: anchor_ once placed, else the last resolved ground
    // point, else the frame's own origin.
    const geo::Vec3 origin = anchor_ ? *anchor_ : (lastGround_ ? *lastGround_ : frame.origin);
    axisLock_ = geo::AxisLock{origin, dir};
    lockedArrowKey_ = key;
}

void LineTool::showConstraintWarning(ToolContext& ctx) const {
    InferenceCue warn;
    // pos anchors the ScreenTip's position -- lastGround_ stands in for
    // "near the cursor", falling back to the world origin.
    warn.pos = lastGround_.value_or(geo::Vec3{});
    warn.warning = true;
    warn.screenTip = "Constraint not appropriate at this time.";
    ctx.setInferenceCue(warn);
}

void LineTool::handleDownArrow(ToolContext& ctx) {
    if (referenceEdge_) {
        referenceEdge_.reset();
        return;
    }

    const geo::Model* model = ctx.model();
    if (!lastRay_ || !model) {
        showConstraintWarning(ctx);
        return;
    }

    // Edge-only pick: vertexTol left at 0 disables the vertex tier (tool.h's
    // "zero a tolerance to disable a pick kind" convention).
    const PointerEvent probe{*lastRay_, QPointF(), lastTols_, false, false, false, 1};
    geo::PickOptions edgeOnly;
    edgeOnly.edgeTol = lastTols_.edgeTol;
    const geo::PickResult hit = ctx.pick(probe, edgeOnly);
    if (hit.kind != geo::PickKind::Edge) {
        showConstraintWarning(ctx);
        return;
    }

    const geo::Edge* edge = model->edge(hit.id);
    const geo::HalfEdge* he0 = edge ? model->halfEdge(edge->halfEdges[0]) : nullptr;
    const geo::HalfEdge* he1 = edge ? model->halfEdge(edge->halfEdges[1]) : nullptr;
    const geo::Vertex* v0 = he0 ? model->vertex(he0->origin) : nullptr;
    const geo::Vertex* v1 = he1 ? model->vertex(he1->origin) : nullptr;
    if (!v0 || !v1) {
        showConstraintWarning(ctx);
        return;
    }

    // Parallel/Perpendicular both additionally require ctx.anchor set --
    // referenceEdge_ alone is inert pre-anchor.
    referenceEdge_ = std::make_pair(v0->pos, v1->pos);
}

void LineTool::cycleLinearInferenceMode(ToolContext& ctx) {
    switch (linearMode_) {
        case LinearInferenceMode::AllOn: linearMode_ = LinearInferenceMode::AllOff; break;
        case LinearInferenceMode::AllOff: linearMode_ = LinearInferenceMode::ParallelPerpendicularOnly; break;
        case LinearInferenceMode::ParallelPerpendicularOnly: linearMode_ = LinearInferenceMode::AllOn; break;
    }
    ctx.setHint(currentHint());
}

void LineTool::onKeyDown(ToolContext& ctx, int key, bool /*ctrl*/) {
    if (key == Qt::Key_Escape) {
        anchor_.reset();
        lastGround_.reset();
        referenceEdge_.reset();
        axisLock_.reset();
        lockedArrowKey_ = 0;
        ctx.setPreview({}, std::nullopt);
        ctx.setInferenceCue(std::nullopt);
        ctx.setHint(currentHint());
        return;
    }

    if (key == Qt::Key_Up || key == Qt::Key_Right || key == Qt::Key_Left) {
        handleArrowLock(ctx, key);
        return;
    }

    if (key == Qt::Key_Down) {
        handleDownArrow(ctx);
        return;
    }

    if (key == Qt::Key_Alt) {
        cycleLinearInferenceMode(ctx);
        return;
    }
}

}  // namespace plnr::tools
