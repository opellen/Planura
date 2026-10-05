#include "tape_measure_tool.h"

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

// Same, no "~ " prefix, for an exact readout not tracking the pointer (hovered edge length; frozen Measure distance).
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

// A guide-line preview spans a fixed length each way from its through-point; must match ui/viewport_presenter.cpp's kGuideLineExtent/kGuideDashLen/kGuideGapLen.
constexpr double kPreviewGuideExtent = 1000.0;
constexpr double kPreviewDashLen = 0.5;
constexpr double kPreviewGapLen = 0.5;

void appendDashedSegment(std::vector<float>& out, const geo::Vec3& from, const geo::Vec3& to) {
    const geo::Vec3 dir = to - from;
    const double totalLen = geo::length(dir);
    if (totalLen < geo::kEps) return;
    const geo::Vec3 unit = dir * (1.0 / totalLen);
    for (double t = 0.0; t < totalLen; t += kPreviewDashLen + kPreviewGapLen) {
        const double segEnd = std::min(t + kPreviewDashLen, totalLen);
        appendVertex(out, from + unit * t);
        appendVertex(out, from + unit * segEnd);
    }
}

}  // namespace

geo::Inference TapeMeasureTool::resolve(ToolContext& ctx, const PointerEvent& e, std::optional<geo::Vec3> anchor) const {
    const geo::Model* model = ctx.model();
    if (!model) return geo::Inference{};

    geo::InferenceContext ictx;
    ictx.anchor = anchor;
    ictx.tols = e.tols;
    ictx.guideLines = ctx.guideLines();
    ictx.guidePoints = ctx.guidePoints();
    // chargedAnchors/referenceEdge left unset (see the header).
    return geo::infer(*model, e.ray, ictx);
}

void TapeMeasureTool::syncCtrl(ToolContext& ctx, bool ctrlNow) {
    if (ctrlNow && !ctrlHeld_) cycleMode(ctx);
    ctrlHeld_ = ctrlNow;
}

void TapeMeasureTool::cycleMode(ToolContext& ctx) {
    switch (mode_) {
        case Mode::CreateGuideLines: mode_ = Mode::CreateGuidePoints; break;
        case Mode::CreateGuidePoints: mode_ = Mode::Measure; break;
        case Mode::Measure: mode_ = Mode::CreateGuideLines; break;
    }
    resetInteraction(ctx);
}

void TapeMeasureTool::resetInteraction(ToolContext& ctx) {
    glArmed_ = false;
    glPerpDir_.reset();
    glLastThrough_.reset();
    glLastDistance_ = 0.0;

    measureStage_ = MeasureStage::Idle;
    measureLast_.reset();
    measuredDistance_ = 0.0;

    ctx.setPreviewBatches({}, std::nullopt);
    ctx.setInferenceCue(std::nullopt);
    ctx.setHint(currentHint());
    ctx.setVcbLabel("Length");
}

std::string TapeMeasureTool::ctrlSuffix() const {
    // UNVERIFIED wording; no confirmed hint text for this tool.
    return " | Ctrl = Cycle Guide Lines / Guide Points / Measure Mode.";
}

std::string TapeMeasureTool::currentHint() const {
    std::string lead;
    switch (mode_) {
        case Mode::CreateGuideLines:
            // UNVERIFIED wording. The unarmed lead also re-shows on a miss-click.
            lead = glArmed_ ? "Click to place the guide line, or type an exact distance."
                             : "Click an edge to create a parallel guide line.";
            break;
        case Mode::CreateGuidePoints:
            lead = "Click to place a guide point.";  // UNVERIFIED
            break;
        case Mode::Measure:
            switch (measureStage_) {
                case MeasureStage::Idle:
                    lead = "Click a point, edge, or guide to measure from.";  // UNVERIFIED
                    break;
                case MeasureStage::Armed:
                    lead = "Click to complete the measurement.";  // UNVERIFIED
                    break;
                case MeasureStage::Frozen: {
                    // measuredDistance_ is embedded (the hint must show the measured value); the rest of the wording is UNVERIFIED.
                    std::ostringstream oss;
                    oss << "Distance: " << std::fixed << std::setprecision(2) << measuredDistance_
                        << ". Type a value to resize the model.";
                    lead = oss.str();
                    break;
                }
            }
            break;
    }
    return lead + ctrlSuffix();
}

bool TapeMeasureTool::tryHoverMeasure(ToolContext& ctx, const PointerEvent& e) const {
    const geo::PickResult hit = ctx.pick(e, e.tols);
    if (hit.kind != geo::PickKind::Edge) return false;

    const geo::Model* model = ctx.model();
    const geo::Edge* edge = model ? model->edge(hit.id) : nullptr;
    const geo::HalfEdge* h0 = edge ? model->halfEdge(edge->halfEdges[0]) : nullptr;
    const geo::HalfEdge* h1 = edge ? model->halfEdge(edge->halfEdges[1]) : nullptr;
    const geo::Vertex* v0 = h0 ? model->vertex(h0->origin) : nullptr;
    const geo::Vertex* v1 = h1 ? model->vertex(h1->origin) : nullptr;
    if (!v0 || !v1) return false;  // defensive -- shouldn't happen for a resolved Edge pick

    ctx.setVcbLabel("Length");
    ctx.setVcbValue(formatExact(geo::distance(v0->pos, v1->pos)));
    return true;
}

geo::Vec3 TapeMeasureTool::defaultPerpDir(const geo::Vec3& dir) const {
    const geo::Vec3 helper = std::fabs(dir.z) > 0.9 ? geo::Vec3{1.0, 0.0, 0.0} : geo::Vec3{0.0, 0.0, 1.0};
    const geo::Vec3 perp = geo::cross(dir, helper);
    const double len = geo::length(perp);
    return len > geo::kEps ? perp * (1.0 / len) : geo::Vec3{1.0, 0.0, 0.0};
}

void TapeMeasureTool::onActivate(ToolContext& ctx) {
    mode_ = Mode::CreateGuideLines;  // onActivate always resets to this mode
    ctrlHeld_ = false;
    resetInteraction(ctx);
}

void TapeMeasureTool::onDeactivate(ToolContext& ctx) {
    ctx.setPreviewBatches({}, std::nullopt);
    ctx.setInferenceCue(std::nullopt);
    glArmed_ = false;
    glPerpDir_.reset();
    glLastThrough_.reset();
    measureStage_ = MeasureStage::Idle;
    measureLast_.reset();
    // mode_ stays: only onActivate resets it to CreateGuideLines.
}

void TapeMeasureTool::onPointerMove(ToolContext& ctx, const PointerEvent& e) {
    syncCtrl(ctx, e.ctrl);
    switch (mode_) {
        case Mode::CreateGuideLines: moveGuideLines(ctx, e); return;
        case Mode::CreateGuidePoints: moveGuidePoints(ctx, e); return;
        case Mode::Measure: moveMeasure(ctx, e); return;
    }
}

void TapeMeasureTool::onPointerDown(ToolContext& ctx, const PointerEvent& e) {
    syncCtrl(ctx, e.ctrl);
    switch (mode_) {
        case Mode::CreateGuideLines: downGuideLines(ctx, e); return;
        case Mode::CreateGuidePoints: downGuidePoints(ctx, e); return;
        case Mode::Measure: downMeasure(ctx, e); return;
    }
}

void TapeMeasureTool::onKeyDown(ToolContext& ctx, int key, bool /*ctrl*/) {
    if (key != Qt::Key_Escape) return;
    resetInteraction(ctx);  // cancel in-progress interaction, keep mode_
}

// ---- CreateGuideLines ----

void TapeMeasureTool::moveGuideLines(ToolContext& ctx, const PointerEvent& e) {
    if (!glArmed_) {
        const geo::Inference inf = resolve(ctx, e, std::nullopt);
        ctx.setPreviewBatches({}, std::nullopt);
        ctx.setInferenceCue(inf.kind != geo::InferenceKind::None ? cueFor(inf) : std::nullopt);
        if (!tryHoverMeasure(ctx, e)) ctx.setVcbLabel("Length");
        ctx.setHint(currentHint());
        return;
    }

    const geo::Inference inf = resolve(ctx, e, glClickPoint_);
    if (inf.kind != geo::InferenceKind::None) {
        const geo::Vec3 toPoint = inf.pos - glClickPoint_;
        const double along = geo::dot(toPoint, glEdgeDir_);
        const geo::Vec3 perp = toPoint - glEdgeDir_ * along;
        const double perpLen = geo::length(perp);
        if (perpLen > geo::kEps) glPerpDir_ = perp * (1.0 / perpLen);
        glLastThrough_ = inf.pos;
        glLastDistance_ = perpLen;
    }
    // Else the ray is parallel to what the ladder would resolve: keep glLastThrough_/glLastDistance_ rather than flicker.

    std::vector<ToolContext::PreviewBatch> batches;
    if (glLastThrough_) {
        std::vector<float> verts;
        appendDashedSegment(verts, *glLastThrough_ - glEdgeDir_ * kPreviewGuideExtent,
                             *glLastThrough_ + glEdgeDir_ * kPreviewGuideExtent);
        batches.push_back(ToolContext::PreviewBatch{std::move(verts), kGuideCueColor.r, kGuideCueColor.g,
                                                      kGuideCueColor.b, kGuideCueColor.a});
    }
    ctx.setPreviewBatches(std::move(batches), glLastThrough_);
    ctx.setInferenceCue(inf.kind != geo::InferenceKind::None ? cueFor(inf) : std::nullopt);
    ctx.setVcbLabel("Distance");
    ctx.setVcbValue(formatApprox(glLastDistance_));
    ctx.setHint(currentHint());
}

void TapeMeasureTool::downGuideLines(ToolContext& ctx, const PointerEvent& e) {
    if (!glArmed_) {
        // Edge-tier-only pick (vertexTol 0); an endpoint click still resolves to that edge.
        geo::PickOptions edgeOnly;
        edgeOnly.edgeTol = e.tols.edgeTol;
        const geo::PickResult hit = ctx.pick(e, edgeOnly);
        if (hit.kind != geo::PickKind::Edge) {
            ctx.setHint(currentHint());  // same instructional lead -- see currentHint()'s own note
            return;
        }

        const geo::Model* model = ctx.model();
        const geo::Edge* edge = model ? model->edge(hit.id) : nullptr;
        const geo::HalfEdge* h0 = edge ? model->halfEdge(edge->halfEdges[0]) : nullptr;
        const geo::HalfEdge* h1 = edge ? model->halfEdge(edge->halfEdges[1]) : nullptr;
        const geo::Vertex* v0 = h0 ? model->vertex(h0->origin) : nullptr;
        const geo::Vertex* v1 = h1 ? model->vertex(h1->origin) : nullptr;
        if (!v0 || !v1) return;  // defensive -- shouldn't happen for a resolved Edge pick

        const geo::Vec3 dir = geo::normalized(v1->pos - v0->pos);
        if (dir.x == 0.0 && dir.y == 0.0 && dir.z == 0.0) return;  // degenerate edge -- defensive

        glClickPoint_ = hit.point;
        glEdgeDir_ = dir;
        glPerpDir_.reset();
        glLastThrough_.reset();
        glLastDistance_ = 0.0;
        glArmed_ = true;
        ctx.setVcbLabel("Distance");
        ctx.setHint(currentHint());
        return;
    }

    // Armed: click 2 commits at the last resolved through-point.
    if (!glLastThrough_) return;  // nothing resolved yet -- stay armed
    ctx.requestAddGuideLine(*glLastThrough_, glEdgeDir_);
    glArmed_ = false;
    glPerpDir_.reset();
    glLastThrough_.reset();
    glLastDistance_ = 0.0;
    ctx.setPreviewBatches({}, std::nullopt);
    ctx.setInferenceCue(std::nullopt);
    ctx.setVcbLabel("Length");
    ctx.setHint(currentHint());
}

// ---- CreateGuidePoints ----

void TapeMeasureTool::moveGuidePoints(ToolContext& ctx, const PointerEvent& e) {
    const geo::Inference inf = resolve(ctx, e, std::nullopt);
    const bool hasTarget = inf.kind != geo::InferenceKind::None;
    ctx.setPreview({}, hasTarget ? std::optional<geo::Vec3>(inf.pos) : std::nullopt);
    ctx.setInferenceCue(hasTarget ? cueFor(inf) : std::nullopt);
    if (!tryHoverMeasure(ctx, e)) ctx.setVcbLabel("Length");
    ctx.setHint(currentHint());
}

void TapeMeasureTool::downGuidePoints(ToolContext& ctx, const PointerEvent& e) {
    const geo::Inference inf = resolve(ctx, e, std::nullopt);
    if (inf.kind == geo::InferenceKind::None) return;
    ctx.requestAddGuidePoint(inf.pos);
    // No arm/mode change -- every click is independent.
}

// ---- Measure ----

void TapeMeasureTool::moveMeasure(ToolContext& ctx, const PointerEvent& e) {
    switch (measureStage_) {
        case MeasureStage::Idle: {
            const geo::Inference inf = resolve(ctx, e, std::nullopt);
            ctx.setPreviewBatches({}, std::nullopt);
            ctx.setInferenceCue(inf.kind != geo::InferenceKind::None ? cueFor(inf) : std::nullopt);
            if (!tryHoverMeasure(ctx, e)) ctx.setVcbLabel("Length");
            ctx.setHint(currentHint());
            return;
        }
        case MeasureStage::Armed: {
            const geo::Inference inf = resolve(ctx, e, measureStart_);
            if (inf.kind != geo::InferenceKind::None) measureLast_ = inf.pos;

            std::vector<ToolContext::PreviewBatch> batches;
            double distance = 0.0;
            if (measureLast_) {
                std::vector<float> verts;
                appendVertex(verts, measureStart_);
                appendVertex(verts, *measureLast_);
                batches.push_back(ToolContext::PreviewBatch{std::move(verts), kDefaultPreviewColor.r,
                                                              kDefaultPreviewColor.g, kDefaultPreviewColor.b,
                                                              kDefaultPreviewColor.a});
                distance = geo::distance(measureStart_, *measureLast_);
            }
            ctx.setPreviewBatches(std::move(batches), measureLast_);
            ctx.setInferenceCue(inf.kind != geo::InferenceKind::None ? cueFor(inf) : std::nullopt);
            ctx.setVcbLabel("Distance");
            ctx.setVcbValue(formatApprox(distance));
            ctx.setHint(currentHint());
            return;
        }
        case MeasureStage::Frozen:
            // Preview/VCB stay as downMeasure's freeze left them; no live tracking while frozen.
            return;
    }
}

void TapeMeasureTool::downMeasure(ToolContext& ctx, const PointerEvent& e) {
    if (measureStage_ == MeasureStage::Armed) {
        const geo::Inference inf = resolve(ctx, e, measureStart_);
        if (inf.kind == geo::InferenceKind::None) return;  // nothing resolved -- stay armed
        measureLast_ = inf.pos;
        measuredDistance_ = geo::distance(measureStart_, *measureLast_);
        measureStage_ = MeasureStage::Frozen;
        ctx.setVcbLabel("Distance");
        ctx.setVcbValue(formatExact(measuredDistance_));
        ctx.setHint(currentHint());  // embeds the measured value -- see currentHint()
        return;
    }

    // Idle or Frozen: a click (re-)arms a new measurement.
    const geo::Inference inf = resolve(ctx, e, std::nullopt);
    if (inf.kind == geo::InferenceKind::None) return;
    measureStart_ = inf.pos;
    measureLast_.reset();
    measuredDistance_ = 0.0;
    measureStage_ = MeasureStage::Armed;
    ctx.setPreviewBatches({}, std::nullopt);
    ctx.setInferenceCue(std::nullopt);
    ctx.setVcbLabel("Distance");
    ctx.setHint(currentHint());
}

void TapeMeasureTool::applyRescale(ToolContext& ctx, double typedValue) {
    if (measuredDistance_ <= geo::kMergeTol) {
        ctx.setHint("Invalid entry.");
        return;
    }
    const double factor = typedValue / measuredDistance_;
    if (std::fabs(factor) <= geo::kEps) {
        // A factor of 0 would collapse every edge to a point; same dialog string as ScaleTool.
        ctx.showWarning("Invalid Scale!");
        return;
    }

    // UNVERIFIED: dialog text/buttons not screenshot-confirmed against the reference modeler.
    if (!ctx.confirm("Do you want to resize the model?")) return;  // declined -- stay Frozen, user may retype

    // Every root edge entity, gathered fresh (not a selection); guides live in GuideStore and are excluded.
    const geo::Model* model = ctx.model();
    std::vector<events::EntityRef> refs;
    if (model) {
        refs.reserve(model->edges().size());
        for (const auto& [id, edge] : model->edges()) {
            (void)edge;
            refs.push_back(events::EntityRef{geo::EntityKind::Edge, id});
        }
    }

    events::TransformSpec spec;
    spec.kind = events::TransformSpec::Kind::Scaling;
    spec.center = geo::Vec3{0.0, 0.0, 0.0};  // world origin
    spec.fx = spec.fy = spec.fz = factor;
    ctx.requestTransformEntities(refs, spec, /*copies=*/0);

    resetInteraction(ctx);  // operation complete -- re-arm for a fresh measurement
}

void TapeMeasureTool::onVcbCommit(ToolContext& ctx, const VcbValue& value) {
    switch (mode_) {
        case Mode::CreateGuideLines: {
            if (!glArmed_ || value.kind != VcbValue::Kind::Scalar) {
                ctx.setHint("Invalid entry.");
                return;
            }
            const geo::Vec3 dir = glPerpDir_.value_or(defaultPerpDir(glEdgeDir_));
            const geo::Vec3 through = glClickPoint_ + dir * value.a;
            ctx.requestAddGuideLine(through, glEdgeDir_);
            // Commits immediately, like a drag-commit click.
            glArmed_ = false;
            glPerpDir_.reset();
            glLastThrough_.reset();
            glLastDistance_ = 0.0;
            ctx.setPreviewBatches({}, std::nullopt);
            ctx.setInferenceCue(std::nullopt);
            ctx.setVcbLabel("Length");
            ctx.setHint(currentHint());
            return;
        }
        case Mode::CreateGuidePoints:
            // No VCB grammar for this mode; single click only.
            ctx.setHint("Invalid entry.");
            return;
        case Mode::Measure:
            if (measureStage_ != MeasureStage::Frozen || value.kind != VcbValue::Kind::Scalar) {
                // No typed-distance commit for the Armed stage either.
                ctx.setHint("Invalid entry.");
                return;
            }
            applyRescale(ctx, value.a);
            return;
    }
}

}  // namespace plnr::tools
