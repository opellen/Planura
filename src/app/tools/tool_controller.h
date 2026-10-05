#pragma once

#include <array>
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <QElapsedTimer>
#include <QPointF>
#include <QVector3D>
#include <Qt>

#include <ordo/core/kernel.h>
#include <ordo/qt/presenter.h>

#include "agent/events.h"
#include "tool.h"
#include "ui/context_menu.h"

namespace plnr::viewport {
class ViewportWidget;
}  // namespace plnr::viewport

namespace plnr::tools {

// Owns one Tool per palette entry; both the Ordo Presenter wiring viewport signals to the active tool and the ToolContext it calls back into.
class ToolController : public ordo::qt::Presenter, public ToolContext {
public:
    ToolController(ordo::core::Kernel& kernel, viewport::ViewportWidget* viewport);

    // Subscribes to ToolChanged and connects the viewport's pointer/key signals. Called by MainWindow once the window tree is finished.
    void onRegister() override;

    // ToolContext
    void requestAddEdge(geo::Vec3 a, geo::Vec3 b) override;
    void requestAddRectangle(geo::Vec3 c1, geo::Vec3 c2) override;
    void requestAddPolyline(const std::vector<geo::Vec3>& points, bool closed) override;
    void requestReplaceLastPolyline(const std::vector<geo::Vec3>& points, bool closed) override;
    void requestExtrudeFace(geo::Id faceId, double distance) override;
    void requestMoveEntity(geo::EntityKind kind, geo::Id id, geo::Vec3 delta) override;
    void requestRemoveEdge(geo::Id edgeId) override;
    void requestTransformEntities(const std::vector<events::EntityRef>& refs, const events::TransformSpec& spec,
                                   int copies) override;
    void requestApplyArrayTimes(int n) override;
    void requestApplyArrayDivide(int n) override;
    void requestFollowMe(geo::Id profileFaceId, const std::vector<geo::Vec3>& pathPoints, bool closedPath) override;
    void requestSelect(events::SelectMode mode, std::optional<events::EntityRef> target,
                        events::SelectExpand expand) override;
    void requestSelectRegion(events::SelectMode mode, const std::array<geo::Ray, 4>& corners, bool crossing) override;
    void requestEnterContext(geo::Id instanceId) override;
    void requestExitContext() override;
    bool atRootContext() const override;
    void requestAddGuideLine(geo::Vec3 point, geo::Vec3 dir) override;
    void requestAddGuidePoint(geo::Vec3 pos) override;
    void requestEraseGuide(geo::Id id) override;
    void requestDeleteAllGuides() override;
    AxesFrame axesFrame() const override;
    void requestSetAxes(geo::Vec3 origin, geo::Vec3 primaryDir, geo::Vec3 secondaryHint) override;
    void requestResetAxes() override;
    void requestAddDimension(geo::Id vertexA, geo::Id vertexB, geo::Vec3 offsetDir, double offset) override;
    void requestAddScreenText(double x, double y, std::string text) override;
    void requestAddLeaderText(geo::Vec3 anchor, std::optional<events::EntityRef> target, std::string text) override;
    void requestSetAnnotationText(geo::Id id, std::string text) override;
    void requestRemoveAnnotation(geo::Id id) override;
    void requestAddSectionPlane(geo::Vec3 point, geo::Vec3 normal, std::string name) override;
    void requestRemoveSectionPlane(geo::Id id) override;
    void requestSetSectionActive(geo::Id id, bool active) override;
    void requestReverseSection(geo::Id id) override;
    std::vector<SectionPlaneData> sections() const override;
    geo::Id activeMaterialId() const override;
    geo::Id frontMaterialOf(events::EntityRef ref) const override;
    void requestPaint(const std::vector<events::EntityRef>& targets, geo::Id materialId) override;
    void requestSetActiveMaterial(geo::Id id) override;
    MaterialTextureInfo materialTextureInfo(geo::Id materialId) const override;
    events::UvTransform faceUvTransform(events::EntityRef ref) const override;
    void requestSetUvTransform(events::EntityRef ref, events::UvTransform transform) override;
    std::optional<std::string> promptText(std::string label, std::string initial) override;
    const geo::Model* model() const override;
    std::vector<geo::GuideLineData> guideLines() const override;
    std::vector<geo::GuidePointData> guidePoints() const override;
    const std::vector<events::EntityRef>& selection() const override;
    geo::PickResult pick(const PointerEvent& e, const geo::PickOptions& opts) const override;
    geo::ScenePickResult pickScene(const PointerEvent& e, const geo::PickOptions& opts) const override;
    geo::Ray makeRay(QPointF screenPos) const override;
    void setPreview(std::vector<float> lineVerts, std::optional<geo::Vec3> marker) override;
    void setPreviewBatches(std::vector<PreviewBatch> batches, std::optional<geo::Vec3> marker) override;
    void setInferenceCue(std::optional<InferenceCue> cue) override;
    // Forwards to ViewportWidget::setHoverFaceTris (call contract: tool.h's ToolContext::setHoverFace).
    void setHoverFace(std::vector<float> faceTris) override;
    // Forwards to kernel_.send(MoveGhostUpdated{...}) -- via the kernel event bus, not viewport_ directly.
    void setMoveGhost(std::vector<geo::Id> vertexIds, geo::Vec3 delta) override;
    void setExtrudeGhost(geo::Id faceId, double distance) override;  // Same event-bus routing as setMoveGhost.
    void setScreenRect(std::optional<QPointF> a, std::optional<QPointF> b) override;
    void setHint(std::string hint) override;
    void showWarning(std::string message) override;
    bool confirm(std::string message) override;
    void setVcbLabel(std::string label) override;
    void setVcbValue(std::string value) override;
    void setSegments(int count) override;

    // -- Solid Tools ---------------------------------------------------------
    void requestSolidOp(events::SolidOp op, std::vector<geo::Id> instanceIds) override;
    void requestToolChange(events::ToolId tool) override;
    geo::Id lastRootInstanceId() const override;
    SolidTargetInfo solidTargetInfo(geo::Id instanceId) const override;
    bool isDefinitionSolid(geo::Id definitionId) const override;

    // ToolId last successfully handed to onToolChanged; MainWindow reads this (menu-only entries are never checkable).
    events::ToolId activeToolId() const { return activeToolId_; }

    // -- Context menu ---------------------------------------------------------
    // Item list for widget-pixel pos (ui::buildContextMenu); the entry point shared by onViewportContextMenu and DebugBridge's `context_menu`.
    std::vector<ui::ContextMenuItem> buildContextMenuItems(QPointF pos);

    // Enters the interposed Divide mode on edgeId (context menu "Divide", real QMenu or DebugBridge).
    void startDivideMode(geo::Id edgeId, QPointF pos);

    // Enters the interposed Position Texture mode on faceId ("Position Texture" item, same two paths).
    void startPositionTextureMode(geo::Id faceId, QPointF pos);

    // -- Debug-only scripted modal support -----------------------------
    // showWarning/confirm/promptText block on a real Qt modal loop the debug bridge cannot answer; this queue pre-supplies an answer.
    struct ScriptedModalAnswer {
        enum Kind { Confirm, Prompt, Warning } kind;
        bool accepted = false;  // Confirm: the return value. Prompt: whether promptText returns text or nullopt.
                                 // Warning: unused (a warning is ack-only).
        std::string text;       // Prompt only (returned when accepted): the scripted entered text. Unused otherwise.
    };

    // Appends one scripted answer; consumed from the front only when its Kind matches the modal that opens.
    void queueModalAnswer(ScriptedModalAnswer answer);

    // Debug bridge: how many scripted answers are pending.
    std::size_t pendingModalAnswers() const;

    // Kind + message of the last opened modal (scripted or real); not cleared on answer.
    struct LastModal {
        ScriptedModalAnswer::Kind kind;
        std::string message;
    };
    std::optional<LastModal> lastModal() const;

private:
    void onToolChanged(const events::ToolChanged& event);
    void onToolSegmentsRequested(const events::ToolSegmentsRequested& event);

    // Forwards a parseVcb result to activeTool_->onVcbCommit(); unparseable text is "Invalid entry.".
    void onVcbCommitted(const events::VcbCommitted& event);

    void onPointerPressed(QPointF pos, Qt::MouseButton button, Qt::KeyboardModifiers modifiers);
    void onPointerMoved(QPointF pos, Qt::KeyboardModifiers modifiers);
    void onPointerReleased(QPointF pos, Qt::MouseButton button, Qt::KeyboardModifiers modifiers);
    void onKeyPressed(int key, Qt::KeyboardModifiers modifiers);

    // ViewportWidget::contextMenuRequested handler: shows a QMenu from buildContextMenuItems(pos); an empty list opens nothing.
    void onViewportContextMenu(QPointF pos);

    // PointerEvent for widget-pixel pos via the viewport's camera; modifiers come from the originating QMouseEvent (bridge input too).
    PointerEvent buildEvent(QPointF pos, Qt::KeyboardModifiers modifiers, int clickCount = 1) const;

    // setZoomAnchorResolver callback ("cursor as zoom center"): scene pick, else ray vs ground z=0, else nullopt (center zoom).
    std::optional<QVector3D> resolveZoomAnchor(const QPointF& pos) const;

    // Divide interaction (value = segment count, VCB "Segments", red-square markers). Not a palette Tool: interposed before
    // activeTool_'s routing, checked first by onPointer*/onKeyPressed/onVcbCommitted.
    class DivideMode {
    public:
        // Arms the mode on edgeId: caches its CURRENT endpoints, seeds count_/VCB/preview. False if edgeId is no longer an edge.
        bool start(ToolController& owner, geo::Id edgeId, const PointerEvent& e);

        void onPointerMove(ToolController& owner, const PointerEvent& e);

        // A plain click commits at the CURRENT live count_ and always ends the interaction.
        bool onPointerDown(ToolController& owner, const PointerEvent& e);

        // Escape cancels (clears preview/VCB/hint, no request); other keys ignored. True iff Escape ended it.
        bool onKeyDown(ToolController& owner, int key);

        // A typed integer commits DivideEdgeRequested{edgeId_, n} regardless of count_. True iff usable; else "Invalid entry." and stays open.
        bool onVcbCommit(ToolController& owner, const VcbValue& value);

        // Cancels silently; does NOT restore activeTool_ (onToolChanged calls it mid-Divide on a tool switch).
        void cancel(ToolController& owner);

    private:
        void clearOverlays(ToolController& owner) const;
        // Re-activates activeTool_ so its hint/VCB label reappear.
        void restoreActiveTool(ToolController& owner) const;
        // Sends DivideEdgeRequested{edgeId_, n} if still valid, then clearOverlays + restoreActiveTool.
        void finish(ToolController& owner, int n);

        geo::Id edgeId_ = geo::kInvalidId;
        geo::Vec3 posA_;
        geo::Vec3 posB_;
        int count_ = 2;
    };

    // Position Texture: fixed-pin "move" only (no rotation/scale pins). Press-drag-release, interposed like DivideMode:
    // onPointerDown anchors, onPointerMove tracks a local delta, onPointerUp commits ONE SetUvTransformRequested. Escape cancels.
    class PositionTextureMode {
    public:
        // Arms the mode on faceId: caches its CURRENT plane, UV basis, tile size and EXISTING UvTransform. False if faceId is no longer a textured face.
        bool start(ToolController& owner, geo::Id faceId, const PointerEvent& e);

        // Hover-only before a drag; once dragging, re-intersects e.ray with the cached plane and refreshes the rubber-band + hint.
        void onPointerMove(ToolController& owner, const PointerEvent& e);

        // The FIRST press anchors the drag; a press mid-drag is ignored. Never ends the interaction (release does).
        void onPointerDown(ToolController& owner, const PointerEvent& e);

        // A release while dragging commits ONE SetUvTransformRequested and ends (true); a stray release is a no-op (false).
        bool onPointerUp(ToolController& owner, const PointerEvent& e);

        // Escape cancels and ends (true) in any state; Return/Enter commits ONLY while dragging.
        bool onKeyDown(ToolController& owner, int key);

        // Cancels silently; does NOT restore activeTool_ (as DivideMode::cancel).
        void cancel(ToolController& owner);

    private:
        void clearOverlays(ToolController& owner) const;
        void restoreActiveTool(ToolController& owner) const;
        // Sends SetUvTransformRequested (offsetU/V shifted by the live delta) if faceId_ is still valid, then clearOverlays + restoreActiveTool.
        void finish(ToolController& owner);

        geo::Id faceId_ = geo::kInvalidId;
        geo::Vec3 planePoint_;
        geo::Vec3 planeNormal_;
        geo::Vec3 uAxis_;
        geo::Vec3 vAxis_;
        double tileW_ = 1.0;
        double tileH_ = 1.0;
        events::UvTransform base_;  // this face's EXISTING transform at start -- rotation/scale carried through unchanged
        bool dragging_ = false;
        geo::Vec3 dragAnchor_;
        double liveDeltaU_ = 0.0;
        double liveDeltaV_ = 0.0;
    };

    std::optional<PositionTextureMode> positionTextureMode_;

    std::optional<DivideMode> divideMode_;

    // Own copy of the kernel reference: Presenter::kernel() is non-const, ToolContext::model() must be const.
    ordo::core::Kernel& kernel_;
    viewport::ViewportWidget* viewport_;

    std::unordered_map<events::ToolId, std::unique_ptr<Tool>> tools_;
    Tool* activeTool_ = nullptr;
    // Mirrors activeTool_ as a ToolId; defaults to Select, the palette's pre-checked action.
    events::ToolId activeToolId_ = events::ToolId::Select;

    // Last text sent via VcbLabelChanged/VcbValueChanged; setVcbLabel/setVcbValue skip kernel_.send() on a match.
    std::string lastVcbLabel_;
    std::string lastVcbValue_;
    // Last segment count sent via ToolSegmentsChanged; reset on tool switch so a new tool always reports.
    int lastSegments_ = 0;

    // Multi-click synthesis (left press): within doubleClickInterval() ms AND 5px increments lastClickCount_ (cap 3); else resets to 1.
    QElapsedTimer lastPressTimer_;
    QPointF lastPressPos_;
    int lastClickCount_ = 0;  // 0 = no previous press seen yet

    // Debug-only scripted modal answers (queueModalAnswer()); empty in every normal run.
    std::deque<ScriptedModalAnswer> modalAnswerQueue_;

    // Kind + message of the last opened modal (lastModal()); nullopt until the first opens.
    std::optional<LastModal> lastModal_;
};

}  // namespace plnr::tools
