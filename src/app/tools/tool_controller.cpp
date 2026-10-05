#include "tool_controller.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <utility>

#include <QApplication>
#include <QInputDialog>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>

#include <geo/model.h>
#include <geo/scene.h>
#include <geo/solid.h>

#include "agent/axes_store.h"
#include "agent/edit_context_store.h"
#include "agent/geometry_api.h"
#include "agent/guide_store.h"
#include "agent/material_repository.h"
#include "agent/section_store.h"
#include "agent/selection_store.h"
#include "agent/tag_store.h"
#include "arc2point_tool.h"
#include "axes_tool.h"
#include "arc3point_tool.h"
#include "arc_center_tool.h"
#include "circle_tool.h"
#include "dimension_tool.h"
#include "eraser_tool.h"
#include "flip_tool.h"
#include "followme_tool.h"
#include "freehand_tool.h"
#include "line_tool.h"
#include "move_tool.h"
#include "offset_tool.h"
#include "paint_bucket_tool.h"
#include "pie_tool.h"
#include "polygon_tool.h"
#include "protractor_tool.h"
#include "pushpull_tool.h"
#include "rectangle_tool.h"
#include "rotate_tool.h"
#include "rotated_rectangle_tool.h"
#include "scale_tool.h"
#include "section_plane_tool.h"
#include "select_tool.h"
#include "solid_tool.h"
#include "tape_measure_tool.h"
#include "text_tool.h"
#include "ui/context_menu.h"
#include "viewport/geo_convert.h"
#include "viewport/material_batch.h"
#include "viewport/viewport_widget.h"

namespace plnr::tools {

namespace {

// DivideMode's segment-count range: 2 is the minimum meaningful division (n < 2 is a no-op); 100 is an approximate max.
constexpr int kMinDivideSegments = 2;
constexpr int kMaxDivideSegments = 100;

// Nearest point on segment [a, b] to ray, clamped to [0,1]; local copy of geo::infer.cpp's closestPointOnSegmentToRay. `t` feeds DivideMode's cursor-to-count.
struct SegmentHit {
    geo::Vec3 pointOnSeg;
    double t{};
};

SegmentHit closestPointOnSegmentToRay(const geo::Vec3& a, const geo::Vec3& b, const geo::Ray& ray) {
    const geo::Vec3 segDir = b - a;
    const double segLenSq = geo::dot(segDir, segDir);
    const geo::Vec3 r = ray.origin - a;
    const double c = geo::dot(ray.dir, r);
    const double f = geo::dot(segDir, r);
    const double bCoef = geo::dot(ray.dir, segDir);
    const double denom = segLenSq - bCoef * bCoef;

    double s = 0.0;
    if (std::fabs(denom) >= geo::kEps) {
        s = std::clamp((f - bCoef * c) / denom, 0.0, 1.0);
    }
    return {a + segDir * s, s};
}

// Deterministic orthonormal basis perpendicular to `dir` (as SectionPlaneTool::planeBasis), here for an EDGE direction: the plane of the division squares.
std::pair<geo::Vec3, geo::Vec3> perpendicularBasis(const geo::Vec3& dir) {
    const geo::Vec3 helper = std::fabs(dir.z) > 0.9 ? geo::Vec3{1.0, 0.0, 0.0} : geo::Vec3{0.0, 0.0, 1.0};
    const geo::Vec3 uRaw = geo::cross(helper, dir);
    const double uLen = geo::length(uRaw);
    const geo::Vec3 u = uLen > geo::kEps ? uRaw * (1.0 / uLen) : geo::Vec3{1.0, 0.0, 0.0};
    const geo::Vec3 v = geo::cross(dir, u);
    return {u, v};
}

// Ray/plane intersection (point + unit normal); nullopt if near-parallel. Deliberately keeps t < 0 so a drag in progress doesn't go dead.
std::optional<geo::Vec3> rayPlaneIntersect(const geo::Vec3& planePoint, const geo::Vec3& planeNormal,
                                            const geo::Ray& ray) {
    const double denom = geo::dot(ray.dir, planeNormal);
    if (std::fabs(denom) < geo::kEps) return std::nullopt;
    const double t = geo::dot(planePoint - ray.origin, planeNormal) / denom;
    return ray.origin + ray.dir * t;
}

void appendVertex(std::vector<float>& verts, const geo::Vec3& p) {
    verts.push_back(static_cast<float>(p.x));
    verts.push_back(static_cast<float>(p.y));
    verts.push_back(static_cast<float>(p.z));
}

void appendSegment(std::vector<float>& verts, const geo::Vec3& a, const geo::Vec3& b) {
    appendVertex(verts, a);
    appendVertex(verts, b);
}

// Small closed square (4 segments) at `center` in the (u, v) plane: DivideMode's stand-in for the reference modeler's red-square markers (no billboard primitive).
void appendSquareMarker(std::vector<float>& verts, const geo::Vec3& center, const geo::Vec3& u, const geo::Vec3& v,
                         double half) {
    const geo::Vec3 uOff = u * half;
    const geo::Vec3 vOff = v * half;
    const geo::Vec3 c1 = center + uOff + vOff;
    const geo::Vec3 c2 = center - uOff + vOff;
    const geo::Vec3 c3 = center - uOff - vOff;
    const geo::Vec3 c4 = center + uOff - vOff;
    appendSegment(verts, c1, c2);
    appendSegment(verts, c2, c3);
    appendSegment(verts, c3, c4);
    appendSegment(verts, c4, c1);
}

// "~ " + 2-decimal formatting, the convention every shape/measure tool keeps locally.
std::string formatApprox(double value) {
    std::ostringstream oss;
    oss << "~ " << std::fixed << std::setprecision(2) << value;
    return oss.str();
}

}  // namespace

ToolController::ToolController(ordo::core::Kernel& kernel, viewport::ViewportWidget* viewport)
    : Presenter(QStringLiteral("ToolController"), viewport), kernel_(kernel), viewport_(viewport) {
    tools_.emplace(events::ToolId::Select, std::make_unique<SelectTool>());
    tools_.emplace(events::ToolId::Line, std::make_unique<LineTool>());
    tools_.emplace(events::ToolId::Rectangle, std::make_unique<RectangleTool>());
    tools_.emplace(events::ToolId::Eraser, std::make_unique<EraserTool>());
    tools_.emplace(events::ToolId::Move, std::make_unique<MoveTool>());
    tools_.emplace(events::ToolId::PushPull, std::make_unique<PushPullTool>());
    tools_.emplace(events::ToolId::Circle, std::make_unique<CircleTool>());
    tools_.emplace(events::ToolId::Polygon, std::make_unique<PolygonTool>());
    tools_.emplace(events::ToolId::Arc2Point, std::make_unique<Arc2PointTool>());
    tools_.emplace(events::ToolId::Arc3Point, std::make_unique<Arc3PointTool>());
    tools_.emplace(events::ToolId::ArcCenter, std::make_unique<ArcCenterTool>());
    tools_.emplace(events::ToolId::Pie, std::make_unique<PieTool>());
    tools_.emplace(events::ToolId::Freehand, std::make_unique<FreehandTool>());
    tools_.emplace(events::ToolId::RotatedRectangle, std::make_unique<RotatedRectangleTool>());
    tools_.emplace(events::ToolId::Rotate, std::make_unique<RotateTool>());
    tools_.emplace(events::ToolId::Scale, std::make_unique<ScaleTool>());
    tools_.emplace(events::ToolId::Offset, std::make_unique<OffsetTool>());
    tools_.emplace(events::ToolId::Flip, std::make_unique<FlipTool>());
    tools_.emplace(events::ToolId::FollowMe, std::make_unique<FollowMeTool>());
    tools_.emplace(events::ToolId::TapeMeasure, std::make_unique<TapeMeasureTool>());
    tools_.emplace(events::ToolId::Protractor, std::make_unique<ProtractorTool>());
    tools_.emplace(events::ToolId::Axes, std::make_unique<AxesTool>());
    tools_.emplace(events::ToolId::Dimension, std::make_unique<DimensionTool>());
    tools_.emplace(events::ToolId::Text, std::make_unique<TextTool>());
    tools_.emplace(events::ToolId::SectionPlane, std::make_unique<SectionPlaneTool>());
    tools_.emplace(events::ToolId::PaintBucket, std::make_unique<PaintBucketTool>());

    // Solid Tools: one SolidTool class parameterized by op, six registrations.
    tools_.emplace(events::ToolId::OuterShell, std::make_unique<SolidTool>(events::SolidOp::OuterShell));
    tools_.emplace(events::ToolId::SolidUnion, std::make_unique<SolidTool>(events::SolidOp::Union));
    tools_.emplace(events::ToolId::SolidSubtract, std::make_unique<SolidTool>(events::SolidOp::Subtract));
    tools_.emplace(events::ToolId::SolidTrim, std::make_unique<SolidTool>(events::SolidOp::Trim));
    tools_.emplace(events::ToolId::SolidIntersect, std::make_unique<SolidTool>(events::SolidOp::Intersect));
    tools_.emplace(events::ToolId::SolidSplit, std::make_unique<SolidTool>(events::SolidOp::Split));
}

void ToolController::onRegister() {
    subscribe<events::ToolChanged>(&ToolController::onToolChanged);
    subscribe<events::VcbCommitted>(&ToolController::onVcbCommitted);
    subscribe<events::ToolSegmentsRequested>(&ToolController::onToolSegmentsRequested);

    connect(viewport_, &viewport::ViewportWidget::pointerPressed, this, &ToolController::onPointerPressed);
    connect(viewport_, &viewport::ViewportWidget::pointerMoved, this, &ToolController::onPointerMoved);
    connect(viewport_, &viewport::ViewportWidget::pointerReleased, this, &ToolController::onPointerReleased);
    connect(viewport_, &viewport::ViewportWidget::keyPressed, this, &ToolController::onKeyPressed);
    connect(viewport_, &viewport::ViewportWidget::contextMenuRequested, this, &ToolController::onViewportContextMenu);

    // Installs the scroll-wheel zoom anchor resolver: a std::function callback (the widget is model-ignorant).
    viewport_->setZoomAnchorResolver([this](const QPointF& pos) { return resolveZoomAnchor(pos); });
}

void ToolController::onToolChanged(const events::ToolChanged& event) {
    // A tool switch mid-Divide cancels it silently; otherwise DivideMode keeps consuming input and strands the new tool.
    if (divideMode_) {
        divideMode_->cancel(*this);
        divideMode_.reset();
    }
    // Likewise a tool switch mid-Position-Texture-drag cancels it silently.
    if (positionTextureMode_) {
        positionTextureMode_->cancel(*this);
        positionTextureMode_.reset();
    }

    if (activeTool_) {
        activeTool_->onDeactivate(*this);
        activeTool_ = nullptr;
    }

    // clickCount carryover guard: click-streak state is controller-wide, so reset here so a fresh tool never inherits the previous streak.
    lastClickCount_ = 0;
    lastPressTimer_.invalidate();
    lastPressPos_ = QPointF();

    auto it = tools_.find(event.tool);
    if (it == tools_.end()) return;  // no Tool implementation yet for this palette entry

    lastSegments_ = 0;
    activeTool_ = it->second.get();
    activeToolId_ = event.tool;
    activeTool_->onActivate(*this);
}

PointerEvent ToolController::buildEvent(QPointF pos, Qt::KeyboardModifiers modifiers, int clickCount) const {
    return PointerEvent{
        viewport_->makeRay(pos),
        pos,
        viewport_->tolerancesAt(viewport_->camera().distance()),
        modifiers.testFlag(Qt::ShiftModifier),
        modifiers.testFlag(Qt::ControlModifier),
        modifiers.testFlag(Qt::AltModifier),
        clickCount,
    };
}

void ToolController::onPointerPressed(QPointF pos, Qt::MouseButton button, Qt::KeyboardModifiers modifiers) {
    // While Divide is open it consumes every left press (click commits at the live count); activeTool_ never sees it.
    if (divideMode_) {
        if (button == Qt::LeftButton && divideMode_->onPointerDown(*this, buildEvent(pos, modifiers))) {
            divideMode_.reset();
        }
        return;
    }
    // A press while Position Texture is open anchors its drag; never ends it (only onPointerReleased/onKeyPressed do).
    if (positionTextureMode_) {
        if (button == Qt::LeftButton) positionTextureMode_->onPointerDown(*this, buildEvent(pos, modifiers));
        return;
    }

    if (button != Qt::LeftButton || !activeTool_) return;

    // clickCount is synthesized from press timing/position (not Qt's double-click event) so bridge-injected rapid presses count too.
    constexpr qreal kClickMoveTolPx = 5.0;
    const bool withinInterval = lastPressTimer_.isValid() && lastPressTimer_.elapsed() <= QApplication::doubleClickInterval();
    const qreal dx = pos.x() - lastPressPos_.x();
    const qreal dy = pos.y() - lastPressPos_.y();
    const bool withinDistance = std::sqrt(dx * dx + dy * dy) <= kClickMoveTolPx;

    if (lastClickCount_ > 0 && withinInterval && withinDistance) {
        lastClickCount_ = lastClickCount_ >= 3 ? 1 : lastClickCount_ + 1;
    } else {
        lastClickCount_ = 1;
    }
    lastPressPos_ = pos;
    lastPressTimer_.start();

    activeTool_->onPointerDown(*this, buildEvent(pos, modifiers, lastClickCount_));
}

void ToolController::onPointerMoved(QPointF pos, Qt::KeyboardModifiers modifiers) {
    // Hover feedback while Divide is open is DivideMode's own, never activeTool_'s.
    if (divideMode_) {
        divideMode_->onPointerMove(*this, buildEvent(pos, modifiers));
        return;
    }
    // Likewise live drag feedback while Position Texture is open.
    if (positionTextureMode_) {
        positionTextureMode_->onPointerMove(*this, buildEvent(pos, modifiers));
        return;
    }
    if (!activeTool_) return;
    activeTool_->onPointerMove(*this, buildEvent(pos, modifiers));
}

void ToolController::onPointerReleased(QPointF pos, Qt::MouseButton button, Qt::KeyboardModifiers modifiers) {
    // DivideMode commits on PRESS: swallow the release so it never reaches activeTool_.
    if (divideMode_) return;
    // Position Texture commits on RELEASE (press-drag-release); the interaction ends here.
    if (positionTextureMode_) {
        if (button == Qt::LeftButton && positionTextureMode_->onPointerUp(*this, buildEvent(pos, modifiers))) {
            positionTextureMode_.reset();
        }
        return;
    }
    if (button != Qt::LeftButton || !activeTool_) return;
    activeTool_->onPointerUp(*this, buildEvent(pos, modifiers));
}

void ToolController::onKeyPressed(int key, Qt::KeyboardModifiers modifiers) {
    if (divideMode_) {
        if (divideMode_->onKeyDown(*this, key)) divideMode_.reset();
        return;
    }
    if (positionTextureMode_) {
        if (positionTextureMode_->onKeyDown(*this, key)) positionTextureMode_.reset();
        return;
    }
    if (!activeTool_) return;
    activeTool_->onKeyDown(*this, key, modifiers.testFlag(Qt::ControlModifier));
}

void ToolController::onVcbCommitted(const events::VcbCommitted& event) {
    const std::optional<VcbValue> parsed = parseVcb(event.text);
    if (!parsed) {
        // Unparseable input: the reference modeler just beeps; here it is surfaced via the status hint (no audio path).
        kernel_.send(events::StatusHintChanged{"Invalid entry."});
        return;
    }
    // A committed VCB value while Divide is open goes to DivideMode's "Segments" field, never activeTool_.
    if (divideMode_) {
        if (divideMode_->onVcbCommit(*this, *parsed)) divideMode_.reset();
        return;
    }
    // PositionTextureMode has no VCB field (du/dv shown via setHint); swallow so no stray commit reaches activeTool_.
    if (positionTextureMode_) return;
    if (activeTool_) activeTool_->onVcbCommit(*this, *parsed);
}

void ToolController::onToolSegmentsRequested(const events::ToolSegmentsRequested& event) {
    if (!activeTool_ || event.tool != activeToolId_) return;
    // Same entry as a typed "Ns": the tool applies its own clamp and retro-edit rules.
    VcbValue value;
    value.kind = VcbValue::Kind::Segments;
    value.count = event.segments;
    activeTool_->onVcbCommit(*this, value);
}

void ToolController::requestAddEdge(geo::Vec3 a, geo::Vec3 b) {
    kernel_.send(events::AddEdgeRequested{a, b});
}

void ToolController::requestAddRectangle(geo::Vec3 c1, geo::Vec3 c2) {
    kernel_.send(events::AddRectangleRequested{c1, c2});
}

void ToolController::requestAddPolyline(const std::vector<geo::Vec3>& points, bool closed) {
    kernel_.send(events::AddPolylineRequested{points, closed});
}

void ToolController::requestReplaceLastPolyline(const std::vector<geo::Vec3>& points, bool closed) {
    kernel_.send(events::ReplaceLastPolylineRequested{points, closed});
}

void ToolController::requestExtrudeFace(geo::Id faceId, double distance) {
    kernel_.send(events::ExtrudeFaceRequested{faceId, distance});
}

void ToolController::requestMoveEntity(geo::EntityKind kind, geo::Id id, geo::Vec3 delta) {
    kernel_.send(events::MoveEntityRequested{kind, id, delta});
}

void ToolController::requestRemoveEdge(geo::Id edgeId) {
    kernel_.send(events::RemoveEdgeRequested{edgeId});
}

void ToolController::requestTransformEntities(const std::vector<events::EntityRef>& refs,
                                               const events::TransformSpec& spec, int copies) {
    kernel_.send(events::TransformEntitiesRequested{refs, spec, copies});
}

void ToolController::requestApplyArrayTimes(int n) {
    kernel_.send(events::ApplyArrayRequested{n, /*divide=*/false});
}

void ToolController::requestApplyArrayDivide(int n) {
    kernel_.send(events::ApplyArrayRequested{n, /*divide=*/true});
}

void ToolController::requestFollowMe(geo::Id profileFaceId, const std::vector<geo::Vec3>& pathPoints,
                                      bool closedPath) {
    kernel_.send(events::FollowMeRequested{profileFaceId, pathPoints, closedPath});
}

void ToolController::requestSelect(events::SelectMode mode, std::optional<events::EntityRef> target,
                                    events::SelectExpand expand) {
    kernel_.send(events::SelectRequested{mode, target, expand});
}

void ToolController::requestSelectRegion(events::SelectMode mode, const std::array<geo::Ray, 4>& corners,
                                          bool crossing) {
    kernel_.send(events::SelectRegionRequested{mode, corners, crossing});
}

void ToolController::requestEnterContext(geo::Id instanceId) {
    kernel_.send(events::EnterContextRequested{instanceId});
}

void ToolController::requestExitContext() {
    kernel_.send(events::ExitContextRequested{});
}

bool ToolController::atRootContext() const {
    auto agent = kernel_.agentAs<agent::EditContextStore>(agent::kEditContextStoreName);
    return !agent || agent->atRoot();
}

void ToolController::requestAddGuideLine(geo::Vec3 point, geo::Vec3 dir) {
    kernel_.send(events::AddGuideLineRequested{point, dir});
}

void ToolController::requestAddGuidePoint(geo::Vec3 pos) {
    kernel_.send(events::AddGuidePointRequested{pos});
}

void ToolController::requestEraseGuide(geo::Id id) {
    kernel_.send(events::EraseGuideRequested{id});
}

void ToolController::requestDeleteAllGuides() {
    kernel_.send(events::DeleteAllGuidesRequested{});
}

AxesFrame ToolController::axesFrame() const {
    auto agent = kernel_.agentAs<agent::AxesStore>(agent::kAxesStoreName);
    if (!agent) return AxesFrame{};  // AxesStore not registered -- world default
    const agent::Frame& f = agent->frame();
    return AxesFrame{f.origin, f.xDir, f.yDir, f.zDir};
}

void ToolController::requestSetAxes(geo::Vec3 origin, geo::Vec3 primaryDir, geo::Vec3 secondaryHint) {
    kernel_.send(events::SetAxesRequested{origin, primaryDir, secondaryHint});
}

void ToolController::requestResetAxes() {
    kernel_.send(events::ResetAxesRequested{});
}

void ToolController::requestAddDimension(geo::Id vertexA, geo::Id vertexB, geo::Vec3 offsetDir, double offset) {
    kernel_.send(events::AddDimensionRequested{vertexA, vertexB, offsetDir, offset});
}

void ToolController::requestAddScreenText(double x, double y, std::string text) {
    kernel_.send(events::AddScreenTextRequested{x, y, std::move(text)});
}

void ToolController::requestAddLeaderText(geo::Vec3 anchor, std::optional<events::EntityRef> target, std::string text) {
    kernel_.send(events::AddLeaderTextRequested{anchor, target, std::move(text)});
}

void ToolController::requestSetAnnotationText(geo::Id id, std::string text) {
    kernel_.send(events::SetAnnotationTextRequested{id, std::move(text)});
}

void ToolController::requestRemoveAnnotation(geo::Id id) {
    kernel_.send(events::RemoveAnnotationRequested{id});
}

void ToolController::requestAddSectionPlane(geo::Vec3 point, geo::Vec3 normal, std::string name) {
    kernel_.send(events::AddSectionPlaneRequested{point, normal, std::move(name)});
}

void ToolController::requestRemoveSectionPlane(geo::Id id) {
    kernel_.send(events::RemoveSectionPlaneRequested{id});
}

void ToolController::requestSetSectionActive(geo::Id id, bool active) {
    kernel_.send(events::SetSectionActiveRequested{id, active});
}

void ToolController::requestReverseSection(geo::Id id) {
    kernel_.send(events::ReverseSectionRequested{id});
}

std::vector<SectionPlaneData> ToolController::sections() const {
    auto agent = kernel_.agentAs<agent::SectionStore>(agent::kSectionStoreName);
    if (!agent) return {};
    std::vector<SectionPlaneData> out;
    out.reserve(agent->planes().size());
    for (const agent::SectionPlane& p : agent->planes()) {
        out.push_back(SectionPlaneData{p.id, p.name, p.point, p.normal, p.active, p.hidden});
    }
    return out;
}

geo::Id ToolController::activeMaterialId() const {
    auto agent = kernel_.agentAs<agent::MaterialRepository>(agent::kMaterialRepositoryName);
    return agent ? agent->activeMaterialId() : geo::Id{0};
}

geo::Id ToolController::frontMaterialOf(events::EntityRef ref) const {
    auto agent = kernel_.agentAs<agent::MaterialRepository>(agent::kMaterialRepositoryName);
    if (!agent) return 0;
    const agent::MaterialAssignment* assignment = agent->assignment(ref);
    return assignment ? assignment->frontMaterialId : geo::Id{0};
}

void ToolController::requestPaint(const std::vector<events::EntityRef>& targets, geo::Id materialId) {
    kernel_.send(events::PaintRequested{targets, materialId});
}

void ToolController::requestSetActiveMaterial(geo::Id id) {
    kernel_.send(events::SetActiveMaterialRequested{id});
}

ToolContext::MaterialTextureInfo ToolController::materialTextureInfo(geo::Id materialId) const {
    auto agent = kernel_.agentAs<agent::MaterialRepository>(agent::kMaterialRepositoryName);
    const agent::Material* m = agent && materialId != 0 ? agent->material(materialId) : nullptr;
    if (!m || m->assetHash.empty()) return {};  // untextured/unknown -- see this method's own header comment
    return {true, m->tileW, m->tileH};
}

events::UvTransform ToolController::faceUvTransform(events::EntityRef ref) const {
    auto agent = kernel_.agentAs<agent::MaterialRepository>(agent::kMaterialRepositoryName);
    const agent::MaterialAssignment* assignment = agent ? agent->assignment(ref) : nullptr;
    return assignment ? assignment->uvTransform : events::UvTransform{};
}

void ToolController::requestSetUvTransform(events::EntityRef ref, events::UvTransform transform) {
    kernel_.send(events::SetUvTransformRequested{ref, transform});
}

std::optional<std::string> ToolController::promptText(std::string label, std::string initial) {
    // lastModal_ records the label shown, however the modal ends up answered.
    lastModal_ = LastModal{ScriptedModalAnswer::Prompt, label};

    // Debug-only scripted answer (queueModalAnswer()'s front-of-queue/kind-match contract); a mismatched or empty queue falls through to the real dialog.
    if (!modalAnswerQueue_.empty() && modalAnswerQueue_.front().kind == ScriptedModalAnswer::Prompt) {
        const ScriptedModalAnswer answer = modalAnswerQueue_.front();
        modalAnswerQueue_.pop_front();
        if (!answer.accepted) return std::nullopt;
        return answer.text;
    }

    // Modal single-line entry; same title convention as showWarning/confirm.
    bool ok = false;
    const QString result =
        QInputDialog::getText(viewport_, QStringLiteral("Planura"), QString::fromStdString(label), QLineEdit::Normal,
                               QString::fromStdString(initial), &ok);
    if (!ok) return std::nullopt;
    return result.toStdString();
}

const geo::Model* ToolController::model() const {
    auto agent = kernel_.agentAs<agent::GeometryApi>(agent::kGeometryApiName);
    if (!agent) return nullptr;
    return &agent->model();
}

std::vector<geo::GuideLineData> ToolController::guideLines() const {
    auto agent = kernel_.agentAs<agent::GuideStore>(agent::kGuideStoreName);
    if (!agent) return {};
    return agent->lineView();
}

std::vector<geo::GuidePointData> ToolController::guidePoints() const {
    auto agent = kernel_.agentAs<agent::GuideStore>(agent::kGuideStoreName);
    if (!agent) return {};
    return agent->pointView();
}

const std::vector<events::EntityRef>& ToolController::selection() const {
    auto agent = kernel_.agentAs<agent::SelectionStore>(agent::kSelectionStoreName);
    if (!agent) {
        static const std::vector<events::EntityRef> kEmpty;
        return kEmpty;
    }
    return agent->items();
}

geo::PickResult ToolController::pick(const PointerEvent& e, const geo::PickOptions& opts) const {
    auto agent = kernel_.agentAs<agent::GeometryApi>(agent::kGeometryApiName);
    if (!agent) return geo::PickResult{};

    // Root model only (context-LOCAL mutation is not supported; pickScene() resolves context). Hidden/tag-invisible entities excluded.
    auto tagStore = kernel_.agentAs<agent::TagStore>(agent::kTagStoreName);
    geo::PickOptions merged = opts;
    const std::function<bool(geo::EntityKind, geo::Id)> callerFilter = opts.filter;
    merged.filter = [agent, tagStore, callerFilter](geo::EntityKind kind, geo::Id id) {
        if (agent->isHidden({kind, id})) return false;
        if (tagStore && !tagStore->isEntityVisible({kind, id})) return false;
        return !callerFilter || callerFilter(kind, id);
    };
    return geo::pick(agent->model(), e.ray, merged);
}

geo::ScenePickResult ToolController::pickScene(const PointerEvent& e, const geo::PickOptions& opts) const {
    auto agent = kernel_.agentAs<agent::GeometryApi>(agent::kGeometryApiName);
    if (!agent) return geo::ScenePickResult{};

    // Same hidden/tag visibility as pick(); geo::pickScene doesn't apply it inside an Instance's interior, so excluded separately below.
    auto tagStore = kernel_.agentAs<agent::TagStore>(agent::kTagStoreName);
    geo::PickOptions merged = opts;
    const std::function<bool(geo::EntityKind, geo::Id)> callerFilter = opts.filter;
    merged.filter = [agent, tagStore, callerFilter](geo::EntityKind kind, geo::Id id) {
        if (agent->isHidden({kind, id})) return false;
        if (tagStore && !tagStore->isEntityVisible({kind, id})) return false;
        return !callerFilter || callerFilter(kind, id);
    };

    // Scopes the pick to the current editing context (empty path without EditContextStore, or at root).
    auto editContextStore = kernel_.agentAs<agent::EditContextStore>(agent::kEditContextStoreName);
    const std::vector<geo::Id> contextPath = editContextStore ? editContextStore->path() : std::vector<geo::Id>{};

    geo::ScenePickResult result = geo::pickScene(agent->scene(), e.ray, merged, /*pickInstances=*/true, contextPath);
    if (result.instanceId != geo::kInvalidId) {
        const events::EntityRef instRef{geo::EntityKind::Instance, result.instanceId};
        const bool instanceHidden = agent->isHidden(instRef) || (tagStore && !tagStore->isEntityVisible(instRef));
        if (instanceHidden) {
            // A hidden/tag-invisible instance isn't pickable as a whole: drop the hit, don't fall back to what's behind.
            return geo::ScenePickResult{};
        }
    }
    return result;
}

geo::Ray ToolController::makeRay(QPointF screenPos) const {
    return viewport_->makeRay(screenPos);
}

std::optional<QVector3D> ToolController::resolveZoomAnchor(const QPointF& pos) const {
    // buildEvent gives the SAME ray/tolerances any tool's click would use.
    const PointerEvent e = buildEvent(pos, Qt::NoModifier);

    // (a) scene-aware pick, same path as SelectTool/SolidTool; ScenePickResult::point is already world-space.
    const geo::ScenePickResult hit = pickScene(e, e.tols);
    if (hit.kind != geo::PickKind::None) {
        return viewport::toQt(hit.point);
    }

    // (b) ray vs ground z=0; unlike PositionTextureMode this also rejects t < 0, falling through to (c).
    const std::optional<geo::Vec3> ground =
        rayPlaneIntersect(geo::Vec3{0.0, 0.0, 0.0}, geo::Vec3{0.0, 0.0, 1.0}, e.ray);
    if (ground) {
        const double t = geo::dot(*ground - e.ray.origin, e.ray.dir);
        if (t >= 0.0) return viewport::toQt(*ground);
    }

    // (c) nothing to anchor on: ViewportWidget::wheelEvent falls back to center zoom (camera_.zoom()).
    return std::nullopt;
}

void ToolController::setPreview(std::vector<float> lineVerts, std::optional<geo::Vec3> marker) {
    // Thin wrapper over setPreviewBatches: one default-colored batch.
    std::vector<PreviewBatch> batches;
    batches.push_back(PreviewBatch{std::move(lineVerts), kDefaultPreviewColor.r, kDefaultPreviewColor.g,
                                    kDefaultPreviewColor.b, kDefaultPreviewColor.a});
    setPreviewBatches(std::move(batches), marker);
}

void ToolController::setPreviewBatches(std::vector<PreviewBatch> batches, std::optional<geo::Vec3> marker) {
    std::optional<QVector3D> qMarker;
    if (marker) qMarker = viewport::toQt(*marker);

    std::vector<viewport::ViewportWidget::PreviewBatch> vpBatches;
    vpBatches.reserve(batches.size());
    for (PreviewBatch& batch : batches) {
        vpBatches.push_back(
            viewport::ViewportWidget::PreviewBatch{std::move(batch.lineVerts), batch.r, batch.g, batch.b, batch.a});
    }
    viewport_->setPreviewBatches(std::move(vpBatches), qMarker);
}

void ToolController::setInferenceCue(std::optional<InferenceCue> cue) {
    // Translates tool.h's InferenceCue into ViewportWidget's Qt-native mirror; the one place crossing that boundary.
    if (!cue) {
        viewport_->setInferenceCue(std::nullopt);
        return;
    }

    viewport::ViewportWidget::InferenceCue vpCue;
    vpCue.pos = viewport::toQt(cue->pos);
    vpCue.shape = cue->shape;
    vpCue.r = cue->color.r;
    vpCue.g = cue->color.g;
    vpCue.b = cue->color.b;
    vpCue.a = cue->color.a;
    vpCue.screenTip = QString::fromStdString(cue->screenTip);
    vpCue.warning = cue->warning;
    if (cue->traceFrom) {
        vpCue.traceFrom = viewport::toQt(*cue->traceFrom);
        const PreviewColor traceColor = cue->traceColor.value_or(kDefaultPreviewColor);
        vpCue.tr = traceColor.r;
        vpCue.tg = traceColor.g;
        vpCue.tb = traceColor.b;
        vpCue.ta = traceColor.a;
    }
    viewport_->setInferenceCue(std::move(vpCue));
}

void ToolController::setHoverFace(std::vector<float> faceTris) {
    // No type translation: both sides share the same GL_TRIANGLES float layout.
    viewport_->setHoverFaceTris(std::move(faceTris));
}

void ToolController::setMoveGhost(std::vector<geo::Id> vertexIds, geo::Vec3 delta) {
    // Unlike the direct viewport_ calls, the move ghost is applied in ViewportPresenter's OWN rebuild via the kernel event bus.
    kernel_.send(events::MoveGhostUpdated{std::move(vertexIds), delta});
}

void ToolController::setExtrudeGhost(geo::Id faceId, double distance) {
    // Same kernel-event routing as setMoveGhost.
    kernel_.send(events::ExtrudeGhostUpdated{faceId, distance});
}

void ToolController::setScreenRect(std::optional<QPointF> a, std::optional<QPointF> b) {
    viewport_->setScreenRect(a, b);
}

void ToolController::setHint(std::string hint) {
    kernel_.send(events::StatusHintChanged{std::move(hint)});
}

void ToolController::showWarning(std::string message) {
    // lastModal_/scripted-answer pattern: see promptText().
    lastModal_ = LastModal{ScriptedModalAnswer::Warning, message};
    if (!modalAnswerQueue_.empty() && modalAnswerQueue_.front().kind == ScriptedModalAnswer::Warning) {
        modalAnswerQueue_.pop_front();  // ack-only -- no return value to consume
        return;
    }

    // Modal, like the reference modeler's dialog for the rare tool errors that use one instead of a status-bar message.
    QMessageBox::warning(viewport_, QStringLiteral("Planura"), QString::fromStdString(message));
}

bool ToolController::confirm(std::string message) {
    // lastModal_/scripted-answer pattern: see promptText().
    lastModal_ = LastModal{ScriptedModalAnswer::Confirm, message};
    if (!modalAnswerQueue_.empty() && modalAnswerQueue_.front().kind == ScriptedModalAnswer::Confirm) {
        const bool accepted = modalAnswerQueue_.front().accepted;
        modalAnswerQueue_.pop_front();
        return accepted;
    }

    // Modal yes/no; same title convention as showWarning.
    const QMessageBox::StandardButton result = QMessageBox::question(
        viewport_, QStringLiteral("Planura"), QString::fromStdString(message), QMessageBox::Yes | QMessageBox::No);
    return result == QMessageBox::Yes;
}

void ToolController::queueModalAnswer(ScriptedModalAnswer answer) {
    modalAnswerQueue_.push_back(std::move(answer));
}

std::size_t ToolController::pendingModalAnswers() const {
    return modalAnswerQueue_.size();
}

std::optional<ToolController::LastModal> ToolController::lastModal() const {
    return lastModal_;
}

void ToolController::setVcbLabel(std::string label) {
    // Dedupe against the last label sent: tools may call this on every pointer-move.
    if (label == lastVcbLabel_) return;
    lastVcbLabel_ = label;
    kernel_.send(events::VcbLabelChanged{std::move(label)});
}

void ToolController::setVcbValue(std::string value) {
    // Same dedupe as setVcbLabel.
    if (value == lastVcbValue_) return;
    lastVcbValue_ = value;
    kernel_.send(events::VcbValueChanged{std::move(value)});
}

void ToolController::setSegments(int count) {
    if (count == lastSegments_) return;
    lastSegments_ = count;
    kernel_.send(events::ToolSegmentsChanged{activeToolId_, count});
}

// -- Solid Tools ---------------------------------------------------------

void ToolController::requestSolidOp(events::SolidOp op, std::vector<geo::Id> instanceIds) {
    kernel_.send(events::SolidOpRequested{op, std::move(instanceIds)});
}

void ToolController::requestToolChange(events::ToolId tool) {
    kernel_.send(events::ToolChanged{tool});
}

geo::Id ToolController::lastRootInstanceId() const {
    auto agent = kernel_.agentAs<agent::GeometryApi>(agent::kGeometryApiName);
    if (!agent) return geo::kInvalidId;
    const std::vector<geo::Instance>& children = agent->scene().root().children;
    return children.empty() ? geo::kInvalidId : children.back().id;
}

ToolContext::SolidTargetInfo ToolController::solidTargetInfo(geo::Id instanceId) const {
    auto agent = kernel_.agentAs<agent::GeometryApi>(agent::kGeometryApiName);
    if (!agent) return {};
    const geo::Instance* inst = agent->scene().findInstance(geo::kRootDefinitionId, instanceId);
    if (!inst) return {};
    const geo::Definition* def = agent->scene().definition(inst->definitionId);
    if (!def) return {};  // defensive only -- Scene guarantees a valid definitionId for any resolved Instance
    return {true, def->id, def->isGroup};
}

bool ToolController::isDefinitionSolid(geo::Id definitionId) const {
    auto agent = kernel_.agentAs<agent::GeometryApi>(agent::kGeometryApiName);
    if (!agent) return false;
    const geo::Definition* def = agent->scene().definition(definitionId);
    return def && geo::isSolidDefinition(*def);
}

// -- Context menu ---------------------------------------------------------

std::vector<ui::ContextMenuItem> ToolController::buildContextMenuItems(QPointF pos) {
    // No modifiers/clickCount: ui::buildContextMenu reads only e.ray/e.tols.
    const PointerEvent e = buildEvent(pos, Qt::NoModifier);
    auto startDivide = [this, pos](geo::Id edgeId) { startDivideMode(edgeId, pos); };
    auto startPositionTexture = [this, pos](geo::Id faceId) { startPositionTextureMode(faceId, pos); };
    return ui::buildContextMenu(*this, context(), e, startDivide, startPositionTexture);
}

void ToolController::startDivideMode(geo::Id edgeId, QPointF pos) {
    DivideMode mode;
    if (mode.start(*this, edgeId, buildEvent(pos, Qt::NoModifier))) {
        divideMode_ = std::move(mode);
    }
    // A false start() (edge vanished, e.g. a bridge mutation) leaves divideMode_ untouched.
}

void ToolController::startPositionTextureMode(geo::Id faceId, QPointF pos) {
    PositionTextureMode mode;
    if (mode.start(*this, faceId, buildEvent(pos, Qt::NoModifier))) {
        positionTextureMode_ = std::move(mode);
    }
    // A false start() (face vanished or no longer textured) leaves positionTextureMode_ untouched.
}

void ToolController::onViewportContextMenu(QPointF pos) {
    const std::vector<ui::ContextMenuItem> items = buildContextMenuItems(pos);
    if (items.empty()) return;  // nothing under the cursor -- see ui::buildContextMenu's own "5. Nothing" case

    QMenu menu(viewport_);
    for (const ui::ContextMenuItem& item : items) {
        QAction* action = menu.addAction(QString::fromStdString(item.label));
        if (item.checkable) {
            action->setCheckable(true);
            action->setChecked(item.checked);
        }
        // Each QAction closes over its own item's action(); no post-exec label matching, so duplicate labels can't misfire.
        connect(action, &QAction::triggered, this, [callback = item.action]() {
            if (callback) callback();
        });
    }
    menu.exec(viewport_->mapToGlobal(pos.toPoint()));
}

// -- DivideMode -------------------------------------------------------------

bool ToolController::DivideMode::start(ToolController& owner, geo::Id edgeId, const PointerEvent& e) {
    const geo::Model* model = owner.model();
    const geo::Edge* edge = model ? model->edge(edgeId) : nullptr;
    if (!edge) return false;
    const geo::HalfEdge* he0 = model->halfEdge(edge->halfEdges[0]);
    const geo::HalfEdge* he1 = model->halfEdge(edge->halfEdges[1]);
    if (!he0 || !he1) return false;
    const geo::Vertex* vA = model->vertex(he0->origin);
    const geo::Vertex* vB = model->vertex(he1->origin);
    if (!vA || !vB) return false;

    edgeId_ = edgeId;
    posA_ = vA->pos;
    posB_ = vB->pos;
    count_ = kMinDivideSegments;

    owner.setHint(
        "Move toward an end to add segments, toward the middle to remove them; click to divide, or enter number of "
        "segments.");
    onPointerMove(owner, e);
    return true;
}

void ToolController::DivideMode::onPointerMove(ToolController& owner, const PointerEvent& e) {
    const SegmentHit hit = closestPointOnSegmentToRay(posA_, posB_, e.ray);

    // distFromMid is 0 at the edge's midpoint (t==0.5), 1 at either end.
    const double distFromMid = std::fabs(hit.t - 0.5) * 2.0;
    constexpr int kSpan = kMaxDivideSegments - kMinDivideSegments;
    count_ = std::clamp(kMinDivideSegments + static_cast<int>(std::lround(distFromMid * kSpan)), kMinDivideSegments,
                         kMaxDivideSegments);

    // Preview: kAxisRedColor squares at each interior division point, in the plane perpendicular to the edge; half-extent is e.tols.vertexTol (constant on-screen size).
    const geo::Vec3 dir = geo::normalized(posB_ - posA_);
    const auto [u, v] = perpendicularBasis(dir);
    std::vector<float> verts;
    for (int k = 1; k < count_; ++k) {
        const double t = static_cast<double>(k) / static_cast<double>(count_);
        appendSquareMarker(verts, posA_ + (posB_ - posA_) * t, u, v, e.tols.vertexTol);
    }
    std::vector<ToolContext::PreviewBatch> batches;
    batches.push_back(
        ToolContext::PreviewBatch{std::move(verts), kAxisRedColor.r, kAxisRedColor.g, kAxisRedColor.b, kAxisRedColor.a});
    owner.setPreviewBatches(std::move(batches), std::nullopt);

    // ScreenTip is ONE line: paintInferenceCue's drawText doesn't lay out an embedded '
    // '. kMarkerNone: the squares carry the marker.
    const double perSeg = count_ > 0 ? geo::distance(posA_, posB_) / count_ : 0.0;
    InferenceCue cue;
    cue.pos = hit.pointOnSeg;
    cue.shape = kMarkerNone;
    cue.screenTip = std::to_string(count_) + " segments, Length: " + formatApprox(perSeg);
    owner.setInferenceCue(cue);

    owner.setVcbLabel("Segments");
    owner.setVcbValue(std::to_string(count_));  // exact count, unprefixed -- same convention Circle/Polygon's Sides stage uses
}

bool ToolController::DivideMode::onPointerDown(ToolController& owner, const PointerEvent& e) {
    onPointerMove(owner, e);  // resolve count_ at the exact click position first
    finish(owner, count_);
    return true;
}

bool ToolController::DivideMode::onKeyDown(ToolController& owner, int key) {
    if (key != Qt::Key_Escape) return false;
    clearOverlays(owner);
    restoreActiveTool(owner);
    return true;
}

bool ToolController::DivideMode::onVcbCommit(ToolController& owner, const VcbValue& value) {
    int n = 0;
    switch (value.kind) {
        case VcbValue::Kind::Scalar:
            if (value.a != std::floor(value.a) || value.a < kMinDivideSegments) {
                owner.setHint("Invalid entry.");
                return false;
            }
            n = static_cast<int>(value.a);
            break;
        case VcbValue::Kind::Segments:
            n = value.count;
            break;
        default:
            owner.setHint("Invalid entry.");
            return false;
    }
    finish(owner, std::clamp(n, kMinDivideSegments, kMaxDivideSegments));
    return true;
}

void ToolController::DivideMode::cancel(ToolController& owner) {
    clearOverlays(owner);
    // Deliberately NOT restoreActiveTool(): the new tool's own onActivate follows and would discard it.
}

void ToolController::DivideMode::clearOverlays(ToolController& owner) const {
    owner.setPreviewBatches({}, std::nullopt);
    owner.setInferenceCue(std::nullopt);
}

void ToolController::DivideMode::restoreActiveTool(ToolController& owner) const {
    // Re-running onActivate is a simplification: safe only while it just resets each tool to its idle stage.
    if (owner.activeTool_) owner.activeTool_->onActivate(owner);
}

void ToolController::DivideMode::finish(ToolController& owner, int n) {
    if (edgeId_ != geo::kInvalidId) {
        owner.kernel_.send(events::DivideEdgeRequested{edgeId_, n});
    }
    clearOverlays(owner);
    restoreActiveTool(owner);
}

// -- PositionTextureMode -----------------------------------------------------

bool ToolController::PositionTextureMode::start(ToolController& owner, geo::Id faceId, const PointerEvent& e) {
    const geo::Model* model = owner.model();
    const geo::Face* face = model ? model->face(faceId) : nullptr;
    if (!face) return false;

    // Defensive re-check: the menu gated on this, but a bridge mutation could have cleared the texture since.
    const geo::Id materialId = owner.frontMaterialOf(events::EntityRef{geo::EntityKind::Face, faceId});
    const ToolContext::MaterialTextureInfo info = owner.materialTextureInfo(materialId);
    if (!info.textured) return false;

    // Root-only pick: the face is root-level geometry, so its (world-space) normal needs no transform.
    const viewport::FaceUvBasis basis = viewport::faceUvBasis(face->normal);
    uAxis_ = basis.uAxis;
    vAxis_ = basis.vAxis;
    planeNormal_ = geo::normalized(face->normal);
    tileW_ = info.tileW;
    tileH_ = info.tileH;
    base_ = owner.faceUvTransform(events::EntityRef{geo::EntityKind::Face, faceId});

    // planePoint_: any point on the face's plane (the boundary loop's first vertex).
    const geo::HalfEdge* he = model->halfEdge(face->halfEdge);
    const geo::Vertex* v = he ? model->vertex(he->origin) : nullptr;
    if (!v) return false;
    planePoint_ = v->pos;

    faceId_ = faceId;
    dragging_ = false;
    liveDeltaU_ = 0.0;
    liveDeltaV_ = 0.0;

    // UNVERIFIED wording; placeholder.
    owner.setHint("Click and drag to move the texture. Press Enter or release to finish, Esc to cancel.");
    return true;
}

void ToolController::PositionTextureMode::onPointerMove(ToolController& owner, const PointerEvent& e) {
    if (!dragging_) return;  // hover-only before the drag actually starts -- see this class's own header comment
    const std::optional<geo::Vec3> hit = rayPlaneIntersect(planePoint_, planeNormal_, e.ray);
    if (!hit) return;  // ray (near-)parallel to the face's plane -- leave the last live delta/preview in place

    const geo::Vec3 deltaWorld = *hit - dragAnchor_;
    const double safeTileW = tileW_ > geo::kEps ? tileW_ : 1.0;
    const double safeTileH = tileH_ > geo::kEps ? tileH_ : 1.0;
    liveDeltaU_ = geo::dot(deltaWorld, uAxis_) / safeTileW;
    liveDeltaV_ = geo::dot(deltaWorld, vAxis_) / safeTileH;

    // Cheap live feedback (the texture itself commits only on release): a rubber-band segment from the anchor to the current point, via setPreviewBatches.
    std::vector<float> verts;
    appendSegment(verts, dragAnchor_, *hit);
    std::vector<ToolContext::PreviewBatch> batches;
    batches.push_back(ToolContext::PreviewBatch{std::move(verts), kDefaultPreviewColor.r, kDefaultPreviewColor.g,
                                                  kDefaultPreviewColor.b, kDefaultPreviewColor.a});
    owner.setPreviewBatches(std::move(batches), *hit);

    std::ostringstream oss;
    oss << std::fixed << std::setprecision(3) << "du " << liveDeltaU_ << ", dv " << liveDeltaV_
        << " -- release or Enter to finish, Esc to cancel.";
    owner.setHint(oss.str());
}

void ToolController::PositionTextureMode::onPointerDown(ToolController& owner, const PointerEvent& e) {
    if (dragging_) return;  // a stray extra press before release -- ignore, the drag is already anchored
    const std::optional<geo::Vec3> hit = rayPlaneIntersect(planePoint_, planeNormal_, e.ray);
    if (!hit) return;  // ray (near-)parallel to the plane at the exact press point -- stay armed, wait for a usable press
    dragAnchor_ = *hit;
    dragging_ = true;
    liveDeltaU_ = 0.0;
    liveDeltaV_ = 0.0;
    owner.setHint("du 0.000, dv 0.000 -- release or Enter to finish, Esc to cancel.");
}

bool ToolController::PositionTextureMode::onPointerUp(ToolController& owner, const PointerEvent& e) {
    if (!dragging_) return false;  // no drag was ever anchored by this release (e.g. a stray release) -- stay armed
    onPointerMove(owner, e);       // resolve the live delta at the exact release position first
    finish(owner);
    return true;
}

bool ToolController::PositionTextureMode::onKeyDown(ToolController& owner, int key) {
    if (key == Qt::Key_Escape) {
        clearOverlays(owner);
        restoreActiveTool(owner);
        return true;
    }
    // Enter commits only a drag already in progress; with none started it is ignored (mode stays armed).
    if ((key == Qt::Key_Return || key == Qt::Key_Enter) && dragging_) {
        finish(owner);
        return true;
    }
    return false;
}

void ToolController::PositionTextureMode::cancel(ToolController& owner) {
    clearOverlays(owner);
}

void ToolController::PositionTextureMode::clearOverlays(ToolController& owner) const {
    owner.setPreviewBatches({}, std::nullopt);
}

void ToolController::PositionTextureMode::restoreActiveTool(ToolController& owner) const {
    // Same simplification as DivideMode::restoreActiveTool.
    if (owner.activeTool_) owner.activeTool_->onActivate(owner);
}

void ToolController::PositionTextureMode::finish(ToolController& owner) {
    if (faceId_ != geo::kInvalidId) {
        events::UvTransform transform = base_;
        transform.offsetU = base_.offsetU + liveDeltaU_;
        transform.offsetV = base_.offsetV + liveDeltaV_;
        owner.requestSetUvTransform(events::EntityRef{geo::EntityKind::Face, faceId_}, transform);
    }
    clearOverlays(owner);
    restoreActiveTool(owner);
}

}  // namespace plnr::tools
