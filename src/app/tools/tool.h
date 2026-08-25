#pragma once

// Tool framework: shared vocabulary between ToolController and each
// concrete Tool. Header-only, Qt/Ordo-light -- tools never see
// ordo/app_kernel.h or the viewport widget directly.

#include <array>
#include <cmath>
#include <optional>
#include <string>
#include <vector>

#include <QPointF>

#include <geo/entity.h>
#include <geo/infer.h>
#include <geo/pick.h>
#include <geo/scene_pick.h>
#include <geo/vec3.h>

#include "agent/events.h"
#include "vcb_parser.h"

namespace plnr::tools {

// Pointer/keyboard input for one tool callback, pre-resolved by
// ToolController into geometry-kernel terms (world-space ray, pick tolerances).
struct PointerEvent {
    geo::Ray ray;
    QPointF screen;
    geo::PickOptions tols;
    bool shift{};  // Shift modifier held at the time of this event (axis lock).
    bool ctrl{};   // Ctrl modifier held at the time of this event (selection add mode).
    bool alt{};    // Alt modifier held at the time of this event (Offset: keep self-overlaps).
    // 1 = single click, 2 = double, 3 = triple (wraps to 1 beyond that);
    // synthesized by ToolController from press timing/position.
    int clickCount{1};
};

// A straight (non-premultiplied) RGBA color for one ToolContext::PreviewBatch.
struct PreviewColor {
    float r, g, b, a;
};

// The viewport's default (uncolored) preview look -- what setPreview's
// implicit batch draws in.
inline constexpr PreviewColor kDefaultPreviewColor{0.0f, 0.0f, 0.0f, 1.0f};

// industry-standard axis-classification colors (Red=X, Green=Y, Blue=Z).
inline constexpr PreviewColor kAxisRedColor{0.86f, 0.20f, 0.18f, 1.0f};
inline constexpr PreviewColor kAxisGreenColor{0.10f, 0.62f, 0.19f, 1.0f};
inline constexpr PreviewColor kAxisBlueColor{0.16f, 0.32f, 0.75f, 1.0f};

// Dominant world axis of a direction (largest-magnitude component decides
// X/Y/Z). Exact only for unit +-X/+-Y/+-Z inputs, not an oblique direction.
enum class WorldAxis { X, Y, Z };

inline WorldAxis dominantWorldAxis(const geo::Vec3& dir) {
    const double ax = std::fabs(dir.x);
    const double ay = std::fabs(dir.y);
    const double az = std::fabs(dir.z);
    if (ax >= ay && ax >= az) return WorldAxis::X;
    if (ay >= az) return WorldAxis::Y;
    return WorldAxis::Z;
}

// dir's matching axis color, used by cueFor() and axis-colored tool previews.
inline PreviewColor axisColorFor(const geo::Vec3& dir) {
    switch (dominantWorldAxis(dir)) {
        case WorldAxis::X: return kAxisRedColor;
        case WorldAxis::Y: return kAxisGreenColor;
        case WorldAxis::Z: return kAxisBlueColor;
    }
    return kAxisRedColor;
}

// dir's matching ScreenTip suffix ("Red Axis"/"Green Axis"/"Blue Axis").
inline const char* axisTipSuffixFor(const geo::Vec3& dir) {
    switch (dominantWorldAxis(dir)) {
        case WorldAxis::X: return "Red Axis";
        case WorldAxis::Y: return "Green Axis";
        case WorldAxis::Z: return "Blue Axis";
    }
    return "Red Axis";
}

// Marker shapes for InferenceCue::shape. kMarkerNone = ScreenTip with no
// marker glyph (line-cue inferences take the axis color instead).
inline constexpr int kMarkerNone = -1;
inline constexpr int kMarkerDot = 0;
inline constexpr int kMarkerDiamond = 1;
inline constexpr int kMarkerSquare = 2;

inline constexpr PreviewColor kEndpointCueColor{0.10f, 0.62f, 0.19f, 1.0f};    // green dot (reuses kAxisGreenColor)
inline constexpr PreviewColor kMidpointCueColor{0.20f, 0.80f, 0.80f, 1.0f};    // cyan dot
inline constexpr PreviewColor kOnFaceCueColor{0.16f, 0.32f, 0.75f, 1.0f};      // blue diamond (reuses kAxisBlueColor)
inline constexpr PreviewColor kFromPointCueColor{0.05f, 0.10f, 0.35f, 1.0f};   // dark navy dot
// Locked-axis rubber-band's endpoint marker: always red regardless of
// which axis is locked (verified).
inline constexpr PreviewColor kLockMarkerColor{0.86f, 0.20f, 0.18f, 1.0f};

inline constexpr PreviewColor kOnEdgeCueColor{0.86f, 0.20f, 0.18f, 1.0f};        // red dot (stub)
inline constexpr PreviewColor kIntersectionCueColor{0.86f, 0.20f, 0.18f, 1.0f};  // red dot (stub)
inline constexpr PreviewColor kGuideCueColor{0.55f, 0.55f, 0.55f, 1.0f};         // gray dot/line (stub)

// A single typed inference-cue overlay: the marker + ScreenTip the
// viewport draws near the current inference point. Set via
// ToolContext::setInferenceCue, independent of setPreviewBatches.
struct InferenceCue {
    geo::Vec3 pos;
    int shape = kMarkerDot;
    PreviewColor color = kDefaultPreviewColor;
    std::string screenTip;
    // true = red screenTip with no marker (the reference modeler's "Constraint not
    // appropriate" case); pos/shape/color are ignored when set.
    bool warning{};
    // Set only for a trace line (FromPoint's charged anchor, or a locked
    // axis's projection) -- draws a dashed line from traceFrom to pos.
    std::optional<geo::Vec3> traceFrom;
    std::optional<PreviewColor> traceColor;
};

inline std::optional<InferenceCue> cueFor(const geo::Inference& inf) {
    switch (inf.kind) {
        case geo::InferenceKind::Endpoint:
            return InferenceCue{inf.pos, kMarkerDot, kEndpointCueColor, "Endpoint"};
        case geo::InferenceKind::Midpoint:
            return InferenceCue{inf.pos, kMarkerDot, kMidpointCueColor, "Midpoint"};
        case geo::InferenceKind::OnFace:
            return InferenceCue{inf.pos, kMarkerDiamond, kOnFaceCueColor, "On Face"};

        case geo::InferenceKind::FromPoint: {
            InferenceCue cue{inf.pos, kMarkerDot, kFromPointCueColor, "From Point"};
            if (inf.source) {
                cue.traceFrom = inf.source;
                cue.traceColor = inf.dir ? axisColorFor(*inf.dir) : kDefaultPreviewColor;
            }
            return cue;
        }

        // Linear inferences carry NO marker (verified) -- the rubber-band
        // line itself takes the axis color; only the ScreenTip is drawn here.
        case geo::InferenceKind::OnAxis: {
            std::string tip = "On ";
            tip += inf.dir ? axisTipSuffixFor(*inf.dir) : "Axis";
            return InferenceCue{inf.pos, kMarkerNone, kDefaultPreviewColor, std::move(tip)};
        }
        // UNVERIFIED tip wording:
        case geo::InferenceKind::Parallel:
            return InferenceCue{inf.pos, kMarkerNone, kDefaultPreviewColor, "Parallel"};
        case geo::InferenceKind::Perpendicular:
            return InferenceCue{inf.pos, kMarkerNone, kDefaultPreviewColor, "Perpendicular"};

        case geo::InferenceKind::OnEdge:
            return InferenceCue{inf.pos, kMarkerDot, kOnEdgeCueColor, "On Edge"};
        case geo::InferenceKind::Intersection:
            return InferenceCue{inf.pos, kMarkerDot, kIntersectionCueColor, "Intersection"};
        case geo::InferenceKind::GuidePoint:
            return InferenceCue{inf.pos, kMarkerDot, kGuideCueColor, "On Point"};
        case geo::InferenceKind::GuideLine:
            return InferenceCue{inf.pos, kMarkerNone, kGuideCueColor, "On Line"};

        case geo::InferenceKind::GroundPlane:
        case geo::InferenceKind::None:
            break;
    }
    return std::nullopt;
}

// Value-view mirror of agent::Frame, so a tool can read the drawing-axes
// frame without depending on agent/axes_store.h. Always orthonormal.
struct AxesFrame {
    geo::Vec3 origin;
    geo::Vec3 xDir{1.0, 0.0, 0.0};
    geo::Vec3 yDir{0.0, 1.0, 0.0};
    geo::Vec3 zDir{0.0, 0.0, 1.0};
};

// Value-view mirror of agent::SectionPlane, decoupling a tool from agent/section_store.h.
struct SectionPlaneData {
    geo::Id id{};
    std::string name;
    geo::Vec3 point;
    geo::Vec3 normal;
    bool active{};
    bool hidden{};
};

// The tool's only door to the world -- mutation requests, model reads,
// preview/hint control. Implemented by ToolController.
class ToolContext {
public:
    virtual ~ToolContext() = default;

    virtual void requestAddEdge(geo::Vec3 a, geo::Vec3 b) = 0;
    virtual void requestAddRectangle(geo::Vec3 c1, geo::Vec3 c2) = 0;

    // Adds a closed-or-open polyline through points -- the shared intent
    // every shape tool funnels through on a committing click.
    virtual void requestAddPolyline(const std::vector<geo::Vec3>& points, bool closed) = 0;

    // Retro-edits the just-committed polyline (the VCB retro-edit window);
    // no-ops silently once that window has closed.
    virtual void requestReplaceLastPolyline(const std::vector<geo::Vec3>& points, bool closed) = 0;

    virtual void requestExtrudeFace(geo::Id faceId, double distance) = 0;
    virtual void requestMoveEntity(geo::EntityKind kind, geo::Id id, geo::Vec3 delta) = 0;
    virtual void requestRemoveEdge(geo::Id edgeId) = 0;

    // Applies spec (rotation/scaling/translation/mirror) to refs' closure.
    // copies == 0 transforms in place; N >= 1 leaves refs untouched, makes N copies.
    virtual void requestTransformEntities(const std::vector<events::EntityRef>& refs, const events::TransformSpec& spec,
                                           int copies) = 0;

    // Replays the open array-copy retro-edit window at n copies (1x..Nx);
    // no-op if that window isn't open. the reference modeler's "*N" VCB entry.
    virtual void requestApplyArrayTimes(int n) = 0;

    // Same as above but divides into n even steps (1/n..n/n) instead of
    // multiplying -- the reference modeler's "/N" VCB entry.
    virtual void requestApplyArrayDivide(int n) = 0;

    // Sweeps profileFaceId's vertex loop along pathPoints (implicitly closed).
    virtual void requestFollowMe(geo::Id profileFaceId, const std::vector<geo::Vec3>& pathPoints, bool closedPath) = 0;

    // Applies a click's selection semantics (see events::SelectRequested's no-target contract).
    virtual void requestSelect(events::SelectMode mode, std::optional<events::EntityRef> target,
                                events::SelectExpand expand) = 0;

    // Applies a drag rectangle's region-select semantics. corners are the 4
    // screen-rect pick rays (TL/TR/BR/BL) -- build via makeRay(), not PointerEvent::ray.
    virtual void requestSelectRegion(events::SelectMode mode, const std::array<geo::Ray, 4>& corners,
                                      bool crossing) = 0;

    // Enters instanceId's group/component editing context. Validates it
    // names a child of the current context -- unknown/foreign id is a no-op.
    virtual void requestEnterContext(geo::Id instanceId) = 0;

    // Exits one level of the current editing context; a no-op at the root.
    virtual void requestExitContext() = 0;

    // True iff the current editing context is the root -- lets a tool
    // branch without seeing EditContextStore directly.
    virtual bool atRootContext() const = 0;

    // Adds an infinite guide line through point along dir (need not be
    // pre-normalized).
    virtual void requestAddGuideLine(geo::Vec3 point, geo::Vec3 dir) = 0;

    // Adds a single guide point at pos.
    virtual void requestAddGuidePoint(geo::Vec3 pos) = 0;

    // Erases one guide (line or point) by id.
    virtual void requestEraseGuide(geo::Id id) = 0;

    // Erases every guide (the reference modeler Edit > Delete Guides).
    virtual void requestDeleteAllGuides() = 0;

    // The current drawing-axes frame; defaults to the world frame when
    // AxesStore isn't registered. Read instead of hardcoding world X/Y/Z.
    virtual AxesFrame axesFrame() const = 0;

    // Sets the drawing-axes frame, re-orthonormalized from primaryDir/
    // secondaryHint. A double-click relocate re-sends the current xDir/yDir.
    virtual void requestSetAxes(geo::Vec3 origin, geo::Vec3 primaryDir, geo::Vec3 secondaryHint) = 0;

    // Resets the drawing axes back to the world default.
    virtual void requestResetAxes() = 0;

    // Adds a linear dimension between model VERTICES vertexA/vertexB,
    // offset by `offset` along unit offsetDir.
    virtual void requestAddDimension(geo::Id vertexA, geo::Id vertexB, geo::Vec3 offsetDir, double offset) = 0;

    // Adds a screen text note fixed at widget-pixel position (x, y).
    virtual void requestAddScreenText(double x, double y, std::string text) = 0;

    // Adds a leader text note anchored at world point anchor, optionally
    // naming the entity it points at.
    virtual void requestAddLeaderText(geo::Vec3 anchor, std::optional<events::EntityRef> target, std::string text) = 0;

    // Replaces annotation id's text (a Dimension's overrideText or a
    // TextNote's own text). Not yet called by any tool.
    virtual void requestSetAnnotationText(geo::Id id, std::string text) = 0;

    // Removes one annotation by id. Not yet called by any tool.
    virtual void requestRemoveAnnotation(geo::Id id) = 0;

    // Adds a section plane through point along normal (need not be
    // pre-normalized). name empty auto-names ("Section Plane N").
    virtual void requestAddSectionPlane(geo::Vec3 point, geo::Vec3 normal, std::string name) = 0;

    // Removes one section plane by id. Unknown id is a no-op.
    virtual void requestRemoveSectionPlane(geo::Id id) = 0;

    // Sets plane id's active flag -- activating one deactivates whatever
    // else was active.
    virtual void requestSetSectionActive(geo::Id id, bool active) = 0;

    // -- Materials --------------------------------------------------------
    // Value-view accessors, not `const MaterialRepository*` (same boundary as above).

    // The Materials panel's current active-material id (0 = none/unregistered).
    virtual geo::Id activeMaterialId() const = 0;

    // ref's front-slot material assignment (0 = unpainted/unregistered) --
    // not instance-inheritance-aware, a raw lookup.
    virtual geo::Id frontMaterialOf(events::EntityRef ref) const = 0;

    // Sets every ref in targets' front material slot to materialId
    // (0 clears it).
    virtual void requestPaint(const std::vector<events::EntityRef>& targets, geo::Id materialId) = 0;

    // Sets the Materials panel's active-material selection -- transient,
    // not undo-captured/dirty-marking.
    virtual void requestSetActiveMaterial(geo::Id id) = 0;

    // Whether materialId names a textured material, plus its tile size --
    // {false, 1.0, 1.0} for materialId == 0 or unrecognized.
    struct MaterialTextureInfo {
        bool textured{};
        double tileW{1.0};
        double tileH{1.0};
    };
    virtual MaterialTextureInfo materialTextureInfo(geo::Id materialId) const = 0;

    // ref's current per-face UV transform (identity if unassigned or
    // MaterialRepository isn't registered). PositionTextureMode::start seeds the
    // drag from this; only offsetU/offsetV accumulate the drag delta.
    virtual events::UvTransform faceUvTransform(events::EntityRef ref) const = 0;

    // Sets ref's per-face UV transform (identity clears it). Sent once
    // per drag commit, never per drag-frame.
    virtual void requestSetUvTransform(events::EntityRef ref, events::UvTransform transform) = 0;

    // Reverses plane id's normal ("Reverse"). Not yet called by any tool.
    virtual void requestReverseSection(geo::Id id) = 0;

    // The current section-plane set; empty when SectionStore isn't
    // registered.
    virtual std::vector<SectionPlaneData> sections() const = 0;

    // Shows a modal single-line text-entry dialog; returns the entered
    // text or nullopt if cancelled. Blocks on its modal loop -- the debug
    // bridge cannot answer a QInputDialog prompt.
    virtual std::optional<std::string> promptText(std::string label, std::string initial) = 0;

    // nullptr if the GeometryApi isn't registered.
    virtual const geo::Model* model() const = 0;

    // Current visible guide lines/points, ready for InferenceContext.
    // Empty when GuideStore isn't registered.
    virtual std::vector<geo::GuideLineData> guideLines() const = 0;
    virtual std::vector<geo::GuidePointData> guidePoints() const = 0;

    // The current selection, read-only. Empty (a static) if
    // SelectionStore isn't registered.
    virtual const std::vector<events::EntityRef>& selection() const = 0;

    // Casts a pick ray against the current model. opts is explicit so a
    // tool can disable a pick kind by zeroing its tolerance. Root-model only.
    virtual geo::PickResult pick(const PointerEvent& e, const geo::PickOptions& opts) const = 0;

    // Container-aware pick: resolves a hit inside a group/component
    // Instance to that Instance's top-level id.
    virtual geo::ScenePickResult pickScene(const PointerEvent& e, const geo::PickOptions& opts) const = 0;

    // World-space pick ray through widget-pixel screenPos, via the
    // viewport's camera.
    virtual geo::Ray makeRay(QPointF screenPos) const = 0;

    // Replaces the viewport's rubber-band/snap-marker overlay ({},
    // nullopt clears it), in the default preview color.
    virtual void setPreview(std::vector<float> lineVerts, std::optional<geo::Vec3> marker) = 0;

    // One preview line batch + its own uniform draw color (straight RGBA).
    // lineVerts is xyz per vertex, 2 vertices per segment (GL_LINES).
    struct PreviewBatch {
        std::vector<float> lineVerts;
        float r, g, b, a;
    };

    // Same overlay as setPreview, but multiple independently colored line
    // batches plus the optional snap marker. Last call wins.
    virtual void setPreviewBatches(std::vector<PreviewBatch> batches, std::optional<geo::Vec3> marker) = 0;

    // Sets (or clears, nullopt) the typed inference-cue overlay, independent
    // of setPreview/setPreviewBatches.
    virtual void setInferenceCue(std::optional<InferenceCue> cue) = 0;

    // Replaces (or clears, empty) the Push/Pull target-face highlight
    // (world-space GL_TRIANGLES). Non-pure (default no-op).
    virtual void setHoverFace(std::vector<float> faceTris) {}

    // Replaces (or clears, empty) the live "move ghost": vertex ids rendered
    // translated by delta during MoveTool's non-copy drag. Non-pure.
    virtual void setMoveGhost(std::vector<geo::Id> vertexIds, geo::Vec3 delta) {}

    // Replaces (or clears, kInvalidId) the live PushPull extrusion ghost:
    // armed face id + the drag's signed distance. Non-pure.
    virtual void setExtrudeGhost(geo::Id faceId, double distance) {}

    // Draws (or clears) the 2D drag-selection rectangle in widget pixels;
    // screen-space, distinct from setPreview's world-space rubber-band.
    virtual void setScreenRect(std::optional<QPointF> a, std::optional<QPointF> b) = 0;

    // Overrides the status bar hint text verbatim (see events::StatusHintChanged).
    virtual void setHint(std::string hint) = 0;

    // Shows a modal warning dialog -- unlike setHint's non-blocking status
    // text, for the rare case the reference modeler pops a blocking dialog.
    virtual void showWarning(std::string message) = 0;

    // Shows a modal yes/no confirm dialog, returns true iff Yes. For a
    // tool needing explicit opt-in before a destructive action.
    virtual bool confirm(std::string message) = 0;

    // Sets the VCB label text (e.g. "Length", "Sides"). May be called every
    // pointer-move even unchanged -- the implementation dedupes.
    virtual void setVcbLabel(std::string label) = 0;

    // Sets the VCB's live value text: "~ " prefixed for an approximate
    // readout, unprefixed for an exact count. Deduped like setVcbLabel.
    virtual void setVcbValue(std::string value) = 0;

    // Runs a solid boolean/split op. Sent by SolidTool's completing
    // second click, and by the context menu's pre-select path.
    virtual void requestSolidOp(events::SolidOp op, std::vector<geo::Id> instanceIds) = 0;

    // The one mechanism a Tool has to switch the palette away from itself.
    // Runs synchronously and calls THIS tool's onDeactivate before
    // returning -- must be the last thing a caller does with `this`.
    virtual void requestToolChange(events::ToolId tool) = 0;

    // The most-recently-appended root Instance id, or kInvalidId if empty.
    // SolidTool calls this before AND after requestSolidOp -- unchanged means rejected.
    virtual geo::Id lastRootInstanceId() const = 0;

    // Cheap classification of a root-level Instance; unresolved reports
    // valid:false. Split from isDefinitionSolid (expensive) for caching.
    struct SolidTargetInfo {
        bool valid{};            // instanceId names a real root-level Instance
        geo::Id definitionId{};  // meaningful only when valid
        bool isGroup{};          // that Definition's own isGroup flag -- meaningful only when valid
    };
    virtual SolidTargetInfo solidTargetInfo(geo::Id instanceId) const = 0;

    // The expensive half of solid classification (walks the whole model).
    // Uncached on purpose -- SolidTool owns its own per-definition cache.
    virtual bool isDefinitionSolid(geo::Id definitionId) const = 0;
};

// One entry in the tool palette. ToolController owns one instance per
// ToolId, driving it: onActivate -> onPointer*/onKeyDown while active ->
// onDeactivate when another tool takes over. Default no-ops.
class Tool {
public:
    virtual ~Tool() = default;

    virtual void onActivate(ToolContext& ctx) {}
    // MUST clear the preview overlay (ctx.setPreview({}, std::nullopt)) so a
    // stale rubber-band/marker doesn't linger after switching tools.
    virtual void onDeactivate(ToolContext& ctx) {}
    virtual void onPointerDown(ToolContext& ctx, const PointerEvent& e) {}
    virtual void onPointerMove(ToolContext& ctx, const PointerEvent& e) {}
    virtual void onPointerUp(ToolContext& ctx, const PointerEvent& e) {}
    // ctrl is the Ctrl modifier's state at the time of this key press.
    virtual void onKeyDown(ToolContext& ctx, int key, bool ctrl) {}

    // Called when the user commits a parsed VCB entry while this tool is
    // active. Default no-op.
    virtual void onVcbCommit(ToolContext& ctx, const VcbValue& value) {}
};

}  // namespace plnr::tools
