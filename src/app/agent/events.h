#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <geo/entity.h>
#include <geo/pick.h>
#include <geo/scene.h>
#include <geo/vec3.h>

namespace plnr::io {
struct OpenPayload;
struct SaveSnapshot;
struct ReadObjResult;
}  // namespace plnr::io

class QByteArray;

namespace plnr::events {

// Which tool is currently active in the main tool palette.
enum class ToolId {
    Select,
    Line,
    Eraser,
    Move,
    Rectangle,
    PushPull,
    Circle,
    Polygon,
    Arc2Point,
    Arc3Point,
    ArcCenter,
    Pie,
    Freehand,
    RotatedRectangle,
    Rotate,
    Scale,
    Offset,
    FollowMe,
    Flip,
    // Guide lines/points + freehand distance measurement; VCB can rescale the model once frozen.
    TapeMeasure,
    // Angle measurement + angled guide-line creation; plane pick via hover/arrow-key lock/Shift/Alt.
    Protractor,
    // industry-standard Axes tool: relocate/reorient the model's drawing axes.
    Axes,
    // Linear dimensions anchored to two model vertices.
    Dimension,
    // Screen text + leader text; double-click a face for an area shortcut.
    Text,
    // Opens Text3dDialog directly; never actually activated as a tool.
    Text3D,
    // Root-context section plane (one active cut); hover/Shift-lock/arrow-orient/click-to-place/double-click-toggle.
    SectionPlane,
    // Paints the active material onto a face/instance; Alt/Shift/Ctrl/Shift+Ctrl vary the scope.
    PaintBucket,

    // Six Solid Tools, all driven by SolidTool parameterized by SolidOp (below).
    OuterShell,
    SolidUnion,
    SolidSubtract,
    SolidTrim,
    SolidIntersect,
    SolidSplit,
};

// Identifies one geometry entity (vertex/edge/face) by kind + id, without depending on geo::Model.
struct EntityRef {
    geo::EntityKind kind{};
    geo::Id id{};

    friend bool operator==(const EntityRef&, const EntityRef&) = default;
};

// How a SelectRequested target combines with the current selection.
enum class SelectMode { Replace, Add, Toggle, Subtract };

// How far a SelectRequested target expands beyond the picked entity.
// Only None is implemented; Attached/Connected are reserved for later.
enum class SelectExpand { None, Attached, Connected };

// Dispatched when the user activates a different tool from the tool palette.
struct ToolChanged {
    static constexpr std::string_view eventName = "ToolChanged";
    ToolId tool;
};

// Context bar -> ToolController: sets the active tool's segment count, routed as the
// same VcbValue{Kind::Segments} an "Ns" VCB entry produces. Ignored unless `tool` is active.
struct ToolSegmentsRequested {
    static constexpr std::string_view eventName = "ToolSegmentsRequested";
    ToolId tool;
    int segments{};
};

// ToolController -> context bar: the active tool's segment count after any change
// (activation, Ctrl+/-, VCB, bar edit). Tools without segments never send it.
struct ToolSegmentsChanged {
    static constexpr std::string_view eventName = "ToolSegmentsChanged";
    ToolId tool;
    int segments{};
};

// Overrides the status bar hint text verbatim, e.g. a mid-interaction prompt.
struct StatusHintChanged {
    static constexpr std::string_view eventName = "StatusHintChanged";
    std::string hint;
};

// Requests GeometryApi to add an edge between two points.
struct AddEdgeRequested {
    static constexpr std::string_view eventName = "AddEdgeRequested";
    geo::Vec3 a;
    geo::Vec3 b;
};

// Requests an axis-aligned rectangle on the ground plane from two diagonal corners.
// z components are ignored (forced to 0).
struct AddRectangleRequested {
    static constexpr std::string_view eventName = "AddRectangleRequested";
    geo::Vec3 corner1;
    geo::Vec3 corner2;
};

// Adds a polyline through points; closed adds a final edge back to the first point.
// Backs every shape tool (circle, polygon, arc, pie, freehand, rotated rectangle).
struct AddPolylineRequested {
    static constexpr std::string_view eventName = "AddPolylineRequested";
    std::vector<geo::Vec3> points;
    bool closed{};
};

// Builds extruded 3D-text geometry as a new component instance.
// outlines are closed glyph polygons in the XY plane (z=0), relative to origin; name empty auto-names.
struct Add3dTextRequested {
    static constexpr std::string_view eventName = "Add3dTextRequested";
    std::string name;
    std::vector<std::vector<geo::Vec3>> outlines;
    double extrusion{};
    geo::Vec3 origin;
};

// Retro-edits the just-committed polyline in place (retype segment count/radius/bulge/angle).
// The retro-edit window is open only right after an addPolyline/replaceLastPolyline call and
// closes on any intervening mutation; an expired window is a silent no-op.
struct ReplaceLastPolylineRequested {
    static constexpr std::string_view eventName = "ReplaceLastPolylineRequested";
    std::vector<geo::Vec3> points;
    bool closed{};
};

// Dispatched by GeometryApi after a mutation that changed the model.
// Empty on purpose; subscribers re-pull current state from GeometryApi.
struct GeometryChanged {
    static constexpr std::string_view eventName = "GeometryChanged";
};

// Extrudes a face along its normal by distance (negative = inward).
struct ExtrudeFaceRequested {
    static constexpr std::string_view eventName = "ExtrudeFaceRequested";
    geo::Id faceId{};
    double distance{};
};

// Moves an entity (vertex/edge/face) by delta.
struct MoveEntityRequested {
    static constexpr std::string_view eventName = "MoveEntityRequested";
    geo::EntityKind kind{};
    geo::Id id{};
    geo::Vec3 delta;
};

// Live move-drag preview: geometry follows the cursor without touching the model until commit.
// vertexIds are the root-model vertices to render translated by delta; empty clears the ghost.
// Fires on every pointer-move; not undo-worthy.
struct MoveGhostUpdated {
    static constexpr std::string_view eventName = "MoveGhostUpdated";
    std::vector<geo::Id> vertexIds;
    geo::Vec3 delta;
};

// PushPullTool's live extrusion preview: the armed drag's current (faceId, distance).
// faceId == kInvalidId clears it; same pure-view-state shape as MoveGhostUpdated.
struct ExtrudeGhostUpdated {
    static constexpr std::string_view eventName = "ExtrudeGhostUpdated";
    geo::Id faceId{geo::kInvalidId};
    double distance{};
};

// Removes an edge, dissolving any face that references it.
struct RemoveEdgeRequested {
    static constexpr std::string_view eventName = "RemoveEdgeRequested";
    geo::Id edgeId{};
};

// Divides edgeId into n equal-length segments. n < 2 or an unknown edgeId is a no-op.
struct DivideEdgeRequested {
    static constexpr std::string_view eventName = "DivideEdgeRequested";
    geo::Id edgeId{};
    int n{};
};

// Dispatched by SelectionStore after a mutation that actually changed the selection set.
// Empty on purpose; subscribers re-pull the current selection.
struct SelectionChanged {
    static constexpr std::string_view eventName = "SelectionChanged";
};

// Applies a click's selection semantics (Select tool).
// target == nullopt: Replace deselects all; any other mode is a no-op.
struct SelectRequested {
    static constexpr std::string_view eventName = "SelectRequested";
    SelectMode mode{SelectMode::Replace};
    std::optional<EntityRef> target;
    SelectExpand expand{SelectExpand::None};
};

// Applies a drag rectangle's region-select semantics.
// corners are the 4 screen-rect corner pick rays, TL/TR/BR/BL.
// crossing: false = window (fully-contained only), true = crossing (touched qualifies).
struct SelectRegionRequested {
    static constexpr std::string_view eventName = "SelectRegionRequested";
    SelectMode mode{SelectMode::Replace};
    std::array<geo::Ray, 4> corners;
    bool crossing{};
};

// Selects every visible entity in the current editing context. Always Replace; no SelectMode field.
struct SelectAllRequested {
    static constexpr std::string_view eventName = "SelectAllRequested";
};

// Hides or unhides the given entities. Unknown refs are ignored.
// Hiding also deselects the hidden entities.
struct SetHiddenRequested {
    static constexpr std::string_view eventName = "SetHiddenRequested";
    std::vector<EntityRef> refs;
    bool hidden{};
};

// Unhides every currently hidden entity.
struct UnhideAllRequested {
    static constexpr std::string_view eventName = "UnhideAllRequested";
};

// Erases the live selection; empty payload, the Command reads SelectionStore at execute time.
// Empty selection is a no-op; per-kind erase rules live in GeometryApi::removeEntities.
struct DeleteSelectionRequested {
    static constexpr std::string_view eventName = "DeleteSelectionRequested";
};

// Creates a new tag (the reference modeler layer). name empty auto-names ("Tag N").
struct TagCreateRequested {
    static constexpr std::string_view eventName = "TagCreateRequested";
    std::string name;
};

// Assigns every ref in refs to tagId. Unknown tagId is a no-op.
// Assigning to the Untagged tag (id 1) reverts refs to the unmapped default.
struct TagAssignRequested {
    static constexpr std::string_view eventName = "TagAssignRequested";
    std::vector<EntityRef> refs;
    std::uint64_t tagId{};
};

// Sets tagId's visibility. Hiding a tag also deselects that tag's currently-selected entities.
struct TagVisibilityRequested {
    static constexpr std::string_view eventName = "TagVisibilityRequested";
    std::uint64_t tagId{};
    bool visible{};
};

// Dispatched by TagStore after the tag list, an assignment, or visibility changed.
// Empty on purpose; subscribers re-pull current tag state.
struct TagsChanged {
    static constexpr std::string_view eventName = "TagsChanged";
};

// Builds a Group or Component from the current selection (read from SelectionStore at execute time).
// name empty auto-names ("Group N"/"Component N").
struct GroupCreateRequested {
    static constexpr std::string_view eventName = "GroupCreateRequested";
    bool asComponent{};
    std::string name;
};

// Explodes instanceId back into loose geometry in the root.
// instanceId must be a direct child of the root; unknown/zero id is a no-op.
struct ExplodeRequested {
    static constexpr std::string_view eventName = "ExplodeRequested";
    geo::Id instanceId{};
};

// Enters instanceId's group/component editing context (double-click-to-edit-in-place).
// instanceId must be a direct child of the current context; unknown/foreign id is a no-op.
// Clears the selection on success.
struct EnterContextRequested {
    static constexpr std::string_view eventName = "EnterContextRequested";
    geo::Id instanceId{};
};

// Exits one level of the current editing context; a no-op at the root context.
// Also clears the selection.
struct ExitContextRequested {
    static constexpr std::string_view eventName = "ExitContextRequested";
};

// Dispatched by EditContextStore after the editing-context path changed (push or pop).
// Empty on purpose; subscribers re-pull the current path.
struct EditContextChanged {
    static constexpr std::string_view eventName = "EditContextChanged";
};

// The user pressed Enter in the VCB (Measurements Box) with the given raw field text.
// Routed to ToolController directly (not a Command) since routing needs its live active-tool state.
// Parse failure reports StatusHintChanged and leaves the field untouched.
struct VcbCommitted {
    static constexpr std::string_view eventName = "VcbCommitted";
    std::string text;
};

// Dispatched when the VCB's meaning changes (e.g. Circle/Polygon: "Sides" -> "Radius").
// May fire redundantly; ToolController dedupes against the last value.
struct VcbLabelChanged {
    static constexpr std::string_view eventName = "VcbLabelChanged";
    std::string label;
};

// Live readout of the current stage's value; prefixed "~ " when it's an approximation.
// Applied only while the VCB field lacks keyboard focus, to never fight the user's typing.
struct VcbValueChanged {
    static constexpr std::string_view eventName = "VcbValueChanged";
    std::string text;
};

// Tagged parametric transform (Kind enum + overlapping fields) covering Translation/Rotation/Scaling/Mirror.
struct TransformSpec {
    enum class Kind { Translation, Rotation, Scaling, Mirror };

    Kind kind{Kind::Translation};

    // Translation
    geo::Vec3 delta;

    // Rotation: point+axis define the rotation line, right-hand rule.
    geo::Vec3 point;
    geo::Vec3 axis;
    double angleRad{};

    // Scaling: world-axis-aligned scale by (fx, fy, fz) about center.
    geo::Vec3 center;
    double fx{1.0};
    double fy{1.0};
    double fz{1.0};

    // Mirror: reflection across the plane through planePoint with normal planeNormal.
    geo::Vec3 planePoint;
    geo::Vec3 planeNormal;

    // Materializes this spec into a geo::Transform scaled by factor (the array-copy step multiplier; at(1.0) is "as specified").
    // Translation scales delta, Rotation scales angleRad; Scaling and Mirror ignore factor.
    geo::Transform at(double factor) const {
        switch (kind) {
            case Kind::Translation:
                return geo::Transform::translation(delta * factor);
            case Kind::Rotation:
                return geo::Transform::rotation(point, axis, angleRad * factor);
            case Kind::Scaling:
                return geo::Transform::scaling(center, fx, fy, fz);
            case Kind::Mirror:
                return geo::Transform::mirror(planePoint, planeNormal);
        }
        return geo::Transform::identity();  // unreachable
    }
};

// Applies spec to the selection closure over refs (geo::closureOf); backs Rotate/Scale/Flip/Move's array-copy.
// copies==0 transforms in place; copies==N>=1 leaves refs untouched and creates N copies.
struct TransformEntitiesRequested {
    static constexpr std::string_view eventName = "TransformEntitiesRequested";
    std::vector<EntityRef> refs;
    TransformSpec spec;
    int copies{};
};

// Replays the currently-open array-copy retro-edit window at n copies (the reference modeler's "*N"/"/N" VCB entry).
// divide selects factors 1/n..n/n; false selects factors 1..n. An expired window is a silent no-op.
struct ApplyArrayRequested {
    static constexpr std::string_view eventName = "ApplyArrayRequested";
    int n{};
    bool divide{};
};

// Sweeps profileFaceId's vertex loop along pathPoints and stitches the result into the model.
// pathPoints is implicitly closed when closedPath; the last point is not repeated.
struct FollowMeRequested {
    static constexpr std::string_view eventName = "FollowMeRequested";
    geo::Id profileFaceId{};
    std::vector<geo::Vec3> pathPoints;
    bool closedPath{};
};

// Adds an infinite guide line through point along dir; dir need not be pre-normalized.
struct AddGuideLineRequested {
    static constexpr std::string_view eventName = "AddGuideLineRequested";
    geo::Vec3 point;
    geo::Vec3 dir;
};

// Adds a single guide point at pos.
struct AddGuidePointRequested {
    static constexpr std::string_view eventName = "AddGuidePointRequested";
    geo::Vec3 pos;
};

// Erases one guide (line or point) by id. Unknown id is a no-op.
struct EraseGuideRequested {
    static constexpr std::string_view eventName = "EraseGuideRequested";
    geo::Id id{};
};

// Erases every guide.
struct DeleteAllGuidesRequested {
    static constexpr std::string_view eventName = "DeleteAllGuidesRequested";
};

// Hides/unhides one guide by id. Unknown id, or a value equal to the current flag, is a no-op.
struct SetGuideHiddenRequested {
    static constexpr std::string_view eventName = "SetGuideHiddenRequested";
    geo::Id id{};
    bool hidden{};
};

// Dispatched by GuideStore after the guide set actually changed.
// Empty on purpose; subscribers re-pull guide state.
struct GuidesChanged {
    static constexpr std::string_view eventName = "GuidesChanged";
};

// Sets the model's drawing-axes frame. primaryDir becomes xDir; secondaryHint derives zDir/yDir.
// A degenerate primaryDir, or a secondaryHint parallel to it, is a no-op.
struct SetAxesRequested {
    static constexpr std::string_view eventName = "SetAxesRequested";
    geo::Vec3 origin;
    geo::Vec3 primaryDir;
    geo::Vec3 secondaryHint;
};

// Resets the drawing axes to the world default (origin zero, xDir/yDir/zDir = world X/Y/Z).
struct ResetAxesRequested {
    static constexpr std::string_view eventName = "ResetAxesRequested";
};

// Dispatched by AxesStore after the frame actually changed (set or reset).
// Empty on purpose; subscribers re-pull the current frame.
struct AxesChanged {
    static constexpr std::string_view eventName = "AxesChanged";
};

// Adds a linear dimension between two model vertices (vertex anchors only: no midpoint/on-edge/arc-center).
// offsetDir is the unit direction (perpendicular to A-B) the dimension line is offset along by offset.
struct AddDimensionRequested {
    static constexpr std::string_view eventName = "AddDimensionRequested";
    geo::Id vertexA{};
    geo::Id vertexB{};
    geo::Vec3 offsetDir;
    double offset{};
};

// Adds a screen text note fixed at widget-pixel position (x, y); never re-anchored to world geometry.
struct AddScreenTextRequested {
    static constexpr std::string_view eventName = "AddScreenTextRequested";
    double x{};
    double y{};
    std::string text;
};

// Adds a leader text note anchored at a resolved world point.
// target optionally names the entity the leader points at; not dynamically tracked.
struct AddLeaderTextRequested {
    static constexpr std::string_view eventName = "AddLeaderTextRequested";
    geo::Vec3 anchor;
    std::optional<EntityRef> target;
    std::string text;
};

// Replaces an annotation's text (a Dimension's overrideText, breaking its dynamic link, or a TextNote's text).
// Unknown id, or a value equal to the current text, is a no-op.
struct SetAnnotationTextRequested {
    static constexpr std::string_view eventName = "SetAnnotationTextRequested";
    geo::Id id{};
    std::string text;
};

// Removes one annotation (dimension or text note) by id. Unknown id is a no-op.
struct RemoveAnnotationRequested {
    static constexpr std::string_view eventName = "RemoveAnnotationRequested";
    geo::Id id{};
};

// Removes every annotation. No-op when already empty.
struct RemoveAllAnnotationsRequested {
    static constexpr std::string_view eventName = "RemoveAllAnnotationsRequested";
};

// Dispatched by AnnotationStore after the annotation set changed, including a refreshAssociations()
// update triggered by GeometryChanged. Empty on purpose; subscribers re-pull current state.
struct AnnotationsChanged {
    static constexpr std::string_view eventName = "AnnotationsChanged";
};

// Adds a new section plane through point along normal; normal need not be pre-normalized.
// name empty auto-names; the new plane starts INACTIVE (SetSectionActiveRequested arms it).
struct AddSectionPlaneRequested {
    static constexpr std::string_view eventName = "AddSectionPlaneRequested";
    geo::Vec3 point;
    geo::Vec3 normal;
    std::string name;
};

// Removes one section plane by id. Unknown id is a no-op.
struct RemoveSectionPlaneRequested {
    static constexpr std::string_view eventName = "RemoveSectionPlaneRequested";
    geo::Id id{};
};

// Sets plane id's active flag; only one plane is active per context, so activating one deactivates the rest.
struct SetSectionActiveRequested {
    static constexpr std::string_view eventName = "SetSectionActiveRequested";
    geo::Id id{};
    bool active{};
};

// Reverses plane id's normal, flipping which side of the cut is removed. Unknown id is a no-op.
struct ReverseSectionRequested {
    static constexpr std::string_view eventName = "ReverseSectionRequested";
    geo::Id id{};
};

// Hides/unhides one plane's own render (widget/outline); a hidden ACTIVE plane keeps cutting.
// Unknown id, or a value equal to the current flag, is a no-op.
struct SetSectionHiddenRequested {
    static constexpr std::string_view eventName = "SetSectionHiddenRequested";
    geo::Id id{};
    bool hidden{};
};

// Dispatched by SectionStore after the section-plane set actually changed.
// Empty on purpose; subscribers re-pull current section state.
struct SectionsChanged {
    static constexpr std::string_view eventName = "SectionsChanged";
};

// Sent at the END of a navigation gesture (drag release, each wheel step) with the camera's full state.
// NOT sent per mouse-move; never marks the document dirty.
struct CameraNavigated {
    static constexpr std::string_view eventName = "CameraNavigated";
    geo::Vec3 target;
    double azimuthDeg{};
    double elevationDeg{};
    double distance{};
    double fovYDeg{};
};

// Dispatched by CameraStore's restore-like methods (restoreCamera/setProjection/setStandardView/previous/next).
// Unlike other Agents' restoreX, these dispatch; set() itself (live nav) dispatches nothing.
struct CameraChanged {
    static constexpr std::string_view eventName = "CameraChanged";
};

// Camera projection modes. Perspective is the default; Parallel is orthographic.
// TwoPoint keeps world-vertical edges vertical on screen; horizontal perspective is unchanged.
enum class Projection { Perspective, Parallel, TwoPoint };

// Switches the active projection mode. NOT undo-wrapped, never marks dirty, but still saved in the .plr.
struct SetProjectionRequested {
    static constexpr std::string_view eventName = "SetProjectionRequested";
    Projection projection{Projection::Perspective};
};

// Seven fixed orientation presets. Enumerator order must match MainWindow::buildMenuBar()'s entry table.
enum class StandardView { Top, Bottom, Front, Back, Left, Right, Iso };

// Jumps to a StandardView preset, setting azimuth/elevation only; target/distance/projection carry over.
// NOT undo-wrapped, never marks dirty; azimuth/elevationDeg still saved in the .plr.
struct SetStandardViewRequested {
    static constexpr std::string_view eventName = "SetStandardViewRequested";
    StandardView view{StandardView::Iso};
};

// Restores the most recent entry from the session-only view-history back stack. No-op when empty.
// Never undo-wrapped; view history is separate from document undo/redo and never persisted.
struct CameraPreviousRequested {
    static constexpr std::string_view eventName = "CameraPreviousRequested";
};

// Mirror of CameraPreviousRequested: restores from the forward stack. No-op when empty; never undo-wrapped.
struct CameraNextRequested {
    static constexpr std::string_view eventName = "CameraNextRequested";
};

// Resets to a brand-new empty document: all six file-format agents to defaults, camera to startup,
// DocumentStore to the unsaved "Untitled" state.
struct NewDocumentRequested {
    static constexpr std::string_view eventName = "NewDocumentRequested";
};

// Loads the .plr document at path, replacing every agent's live state wholesale (all-or-nothing).
struct OpenDocumentRequested {
    static constexpr std::string_view eventName = "OpenDocumentRequested";
    std::string path;
};

// Saves the live document to path as a .plr file; MainWindow resolves Save vs. Save As before sending.
struct SaveDocumentRequested {
    static constexpr std::string_view eventName = "SaveDocumentRequested";
    std::string path;
};

// Async Open, main-thread half: the worker already read, sniffed, extracted and parsed the file; this applies it as
// OpenDocumentRequested would after its file IO. The payload is opaque here (plnr_agent stays Qt-free), defined in io/plr_container.h.
struct OpenDocumentDataReady {
    static constexpr std::string_view eventName = "OpenDocumentDataReady";
    std::string path;
    std::shared_ptr<io::OpenPayload> payload;
};

// Async Save, step 1: captures the value snapshot synchronously on the main thread and answers
// with DocumentSnapshotReady. Never writes to disk.
struct SaveSnapshotRequested {
    static constexpr std::string_view eventName = "SaveSnapshotRequested";
    std::string path;
};

// Answer to SaveSnapshotRequested; consumed by ui::IoPresenter. revision is DocumentStore::revision()
// at snapshot time, handed back through SaveCommitted so edits landing mid-save are detected.
struct DocumentSnapshotReady {
    static constexpr std::string_view eventName = "DocumentSnapshotReady";
    std::string path;
    std::shared_ptr<const io::SaveSnapshot> snapshot;
    std::uint64_t revision{};
};

// Async Save, step 2: the worker wrote path successfully. Clears dirty only if no mutation
// landed since the snapshot (revision still current); otherwise records the path and stays dirty.
struct SaveCommitted {
    static constexpr std::string_view eventName = "SaveCommitted";
    std::string path;
    std::uint64_t revision{};
};

// Dispatched by DocumentStore's mutators: markDirty() only on a false->true transition;
// setSaved()/resetNew() unconditionally. Empty on purpose; subscribers re-pull filePath()/dirty().
struct DocumentStateChanged {
    static constexpr std::string_view eventName = "DocumentStateChanged";
};

// Dispatched by io Commands when a file operation fails, alongside a matching StatusHintChanged.
// No subscriber yet; exists so tests can assert the failure by type, not by scraping status text.
struct DocumentIoFailed {
    static constexpr std::string_view eventName = "DocumentIoFailed";
    std::string message;
};

// Restores the most recently committed snapshot. No-op when canUndo() is false.
// Never wrapped by UndoCaptureCommand; undoing must not itself create a new undo step.
struct UndoRequested {
    static constexpr std::string_view eventName = "UndoRequested";
};

// Re-applies the most recently undone snapshot. No-op when canRedo() is false.
// Never wrapped by UndoCaptureCommand, same reasoning as UndoRequested.
struct RedoRequested {
    static constexpr std::string_view eventName = "RedoRequested";
};

// Dispatched whenever canUndo()/canRedo() actually changes. Empty on purpose; subscribers re-pull both.
struct UndoStateChanged {
    static constexpr std::string_view eventName = "UndoStateChanged";
};

// Exports the live document's VISIBLE geometry to an OBJ file at path. Read-only: never marks dirty.
// A write failure dispatches DocumentIoFailed.
struct ExportObjRequested {
    static constexpr std::string_view eventName = "ExportObjRequested";
    std::string path;
};

// Imports the OBJ file at path as one new component Definition + one Instance at the root origin.
// Undoable and marks dirty like any geometry-creating event. A parse failure dispatches
// DocumentIoFailed and creates nothing.
struct ImportObjRequested {
    static constexpr std::string_view eventName = "ImportObjRequested";
    std::string path;
};

// Async Import, main-thread half: the worker already read and parsed the file (payload is an ok io::ReadObjResult,
// opaque here -- plnr_agent stays Qt-free); applies it as ImportObjRequested would after its file IO.
// name is the new component's name (file name sans last extension). Undoable and dirty-marking.
struct ImportObjDataReady {
    static constexpr std::string_view eventName = "ImportObjDataReady";
    std::string path;
    std::string name;
    std::shared_ptr<const io::ReadObjResult> payload;
};

// Async Export, step 1: serializes the live visible geometry on the main thread and answers with
// ObjBytesReady. Never writes to disk; read-only like ExportObjRequested.
struct ExportObjSnapshotRequested {
    static constexpr std::string_view eventName = "ExportObjSnapshotRequested";
    std::string path;
};

// Answer to ExportObjSnapshotRequested; consumed by ui::IoPresenter. Carries no revision:
// export never touches document state, so there is nothing to reconcile on completion.
struct ObjBytesReady {
    static constexpr std::string_view eventName = "ObjBytesReady";
    std::string path;
    std::shared_ptr<const QByteArray> bytes;
};

// Creates a new material with the given color/opacity. name empty auto-names ("Material N").
struct MaterialCreateRequested {
    static constexpr std::string_view eventName = "MaterialCreateRequested";
    std::string name;
    double r{};
    double g{};
    double b{};
    double opacity{1.0};
};

// Edits an existing material's name/color/opacity in place.
// Unknown id, or a call whose fields already match the current value, is a no-op.
struct MaterialEditRequested {
    static constexpr std::string_view eventName = "MaterialEditRequested";
    geo::Id id{};
    std::string name;
    double r{};
    double g{};
    double b{};
    double opacity{1.0};
};

// Sets every ref in targets' FRONT material slot to materialId (0 clears it). Front slot only.
// An unknown nonzero materialId rejects the whole call.
struct PaintRequested {
    static constexpr std::string_view eventName = "PaintRequested";
    std::vector<EntityRef> targets;
    geo::Id materialId{};  // 0 = clear
};

// Sets the Materials panel's active-material selection. Purely transient UI state: not undo-wrapped, never marks dirty.
struct SetActiveMaterialRequested {
    static constexpr std::string_view eventName = "SetActiveMaterialRequested";
    geo::Id id{};
};

// Dispatched by MaterialRepository after the material list or an assignment changed (not setActive()).
// Empty on purpose; subscribers re-pull current state.
struct MaterialsChanged {
    static constexpr std::string_view eventName = "MaterialsChanged";
};

// Sets or clears materialId's texture; path empty clears it. Only png/jpg/jpeg accepted -- other
// suffixes, or an unopenable file, reject the whole call. tileW/tileH apply only when path is non-empty.
struct MaterialSetTextureRequested {
    static constexpr std::string_view eventName = "MaterialSetTextureRequested";
    geo::Id materialId{};
    std::string path;  // empty = clear texture
    double tileW{1.0};
    double tileH{1.0};
};

// Per-face 2D UV transform for the FRONT slot's texture coordinates only. Identity ({0,0,0,1,1}) means "no transform":
// the no-op check and the .plr omit-when-identity rule both key on it (isIdentityUvTransform).
// Applied translate-rotate-scale in UV space: uv' = S(1/scaleU,1/scaleV) * R(-rotationRad) * (uv - offset).
struct UvTransform {
    double offsetU{};
    double offsetV{};
    double rotationRad{};
    double scaleU{1.0};
    double scaleV{1.0};
};

// True iff t is the identity (exact equality is safe: writers copy verbatim or default-construct). The "invisible in
// the .plr" test, distinct from setUvTransform's field-by-field no-op check.
inline bool isIdentityUvTransform(const UvTransform& t) {
    return t.offsetU == 0.0 && t.offsetV == 0.0 && t.rotationRad == 0.0 && t.scaleU == 1.0 && t.scaleV == 1.0;
}

// Sets ref's per-face UV transform (front slot only). Passing the identity value clears it; no
// separate "clear" intent. ref carrying no material assignment rejects the whole call.
struct SetUvTransformRequested {
    static constexpr std::string_view eventName = "SetUvTransformRequested";
    EntityRef ref;
    UvTransform transform;
};

// Six exclusive Face Style modes. Enumerator order must stay identical across menu, .plr schema,
// and bridge protocol. ShadedWithTextures is the default.
enum class FaceStyle { Wireframe, HiddenLine, Shaded, ShadedWithTextures, Monochrome, XRay };

// Which edge-appearance toggle SetEdgeStyleFlagRequested targets. Extensions/Endpoints/Jitter are not modeled.
enum class EdgeFlag { Profiles, DepthCue, BackEdges };

// Switches the active Face Style. NOT undo-wrapped; view-setting semantics, not document content.
struct SetFaceStyleRequested {
    static constexpr std::string_view eventName = "SetFaceStyleRequested";
    FaceStyle style{FaceStyle::ShadedWithTextures};
};

// Sets one edge-style flag (Kind enum + value idiom, covering all three EdgeFlag values). NOT undo-wrapped.
struct SetEdgeStyleFlagRequested {
    static constexpr std::string_view eventName = "SetEdgeStyleFlagRequested";
    EdgeFlag flag{EdgeFlag::Profiles};
    bool value{};
};

// Toggles the viewport SSAO effect on/off. NOT undo-wrapped, same rationale as SetFaceStyleRequested.
struct SetAmbientOcclusionRequested {
    static constexpr std::string_view eventName = "SetAmbientOcclusionRequested";
    bool ambientOcclusion{};
};

// Sets the AO multiply-blend strength (0-1, unvalidated). Debug-only: no UI control yet. NOT undo-wrapped.
struct SetAoStrengthRequested {
    static constexpr std::string_view eventName = "SetAoStrengthRequested";
    double aoStrength{};
};

// Dispatched by StyleStore after the face style, an edge flag, or AO toggle/strength changed.
// Empty on purpose; subscribers re-pull current style state.
struct StyleChanged {
    static constexpr std::string_view eventName = "StyleChanged";
};

// Dispatched after CameraNavigated produces a real CameraStore state change.
// Lighter than GeometryChanged: only re-classifies cached per-edge adjacency against the new eye
// position. Fired only at gesture end, so classification lags during a drag.
struct CameraMoved {
    static constexpr std::string_view eventName = "CameraMoved";
};

// -- Shadows -----------------------------------------------------------
// the reference modeler's two separate checkboxes ("Show/Hide Shadows", "Use sun for shading") map to the two Requested events below.

// Toggles N.L sun-position face shading on/off; does NOT imply ground-plane shadow casting. NOT undo-wrapped.
struct SetUseSunForShadingRequested {
    static constexpr std::string_view eventName = "SetUseSunForShadingRequested";
    bool useSunForShading{};
};

// Toggles ground-plane shadow casting on/off. NOT undo-wrapped.
// The shadow pass is gated on this AND the sun being above the horizon.
struct SetShowShadowsRequested {
    static constexpr std::string_view eventName = "SetShowShadowsRequested";
    bool showShadows{};
};

// Sets the sun's geographic position. Debug-only: no UI control yet. NOT undo-wrapped.
struct SetSunPositionRequested {
    static constexpr std::string_view eventName = "SetSunPositionRequested";
    double latitudeDeg{};
    double longitudeDeg{};
};

// Sets the sun's calendar date/time. month 1-12, day 1-31, hourLocal is apparent solar hours 0-24
// (see sun_position.h for why there's no timezone field). Debug-only. NOT undo-wrapped.
struct SetSunDateTimeRequested {
    static constexpr std::string_view eventName = "SetSunDateTimeRequested";
    int month{};
    int day{};
    double hourLocal{};
};

// Sets the Light slider (0-100, unvalidated). Debug-only: no UI control yet. NOT undo-wrapped.
struct SetShadowLightRequested {
    static constexpr std::string_view eventName = "SetShadowLightRequested";
    double light{};
};

// Sets the Dark slider (0-100, unvalidated). Debug-only. NOT undo-wrapped.
struct SetShadowDarkRequested {
    static constexpr std::string_view eventName = "SetShadowDarkRequested";
    double dark{};
};

// Dispatched by ShadowStore after any shadow setting actually changed. Empty on purpose; subscribers re-pull current state.
struct ShadowsChanged {
    static constexpr std::string_view eventName = "ShadowsChanged";
};

// -- Fog -------------------------------------------------------------------
// Fragment-shader depth fog: fades model faces/edges toward a fog color as eye distance grows.
// Its own Agent (not folded into ShadowStore) since the mirror treats Fog and Shadows as separate panels.

// Toggles fog on/off. NOT undo-wrapped; view-setting semantics.
struct SetFogEnabledRequested {
    static constexpr std::string_view eventName = "SetFogEnabledRequested";
    bool enabled{};
};

// Sets the fog start/end distances (world units from the eye, fixed-distance). Debug-only. NOT undo-wrapped.
struct SetFogRangeRequested {
    static constexpr std::string_view eventName = "SetFogRangeRequested";
    double startDistance{};
    double endDistance{};
};

// Sets whether fog color follows the viewport background (default) or a reserved custom color
// (no color-picker UI yet). Debug-only. NOT undo-wrapped.
struct SetFogUseBackgroundColorRequested {
    static constexpr std::string_view eventName = "SetFogUseBackgroundColorRequested";
    bool useBackgroundColor{};
};

// Dispatched by FogStore after enabled/range/useBackgroundColor actually changed. Empty on purpose.
struct FogChanged {
    static constexpr std::string_view eventName = "FogChanged";
};

// the reference modeler's six Solid Tools, each a CSG boolean/split (geo::csg) over root-level Instances.
enum class SolidOp { Union, OuterShell, Subtract, Trim, Intersect, Split };

// Runs a solid boolean/split over root-level Instances named by instanceIds, in CLICK ORDER --
// for Subtract/Trim the FIRST id is the cutter, the LAST is the target.
// Union/OuterShell/Intersect need >= 2 operands; Subtract/Trim/Split need exactly 2.
// A violation, or a non-overlapping pair, reports StatusHintChanged with no mutation.
struct SolidOpRequested {
    static constexpr std::string_view eventName = "SolidOpRequested";
    SolidOp op{SolidOp::Union};
    std::vector<geo::Id> instanceIds;
};

}  // namespace plnr::events

namespace std {

// Combines EntityRef's two fields for SelectionStore's unordered_set<EntityRef> (O(1) membership).
template <>
struct hash<plnr::events::EntityRef> {
    std::size_t operator()(const plnr::events::EntityRef& ref) const noexcept {
        return static_cast<std::size_t>(ref.id) * 31 + static_cast<std::size_t>(ref.kind);
    }
};

}  // namespace std
