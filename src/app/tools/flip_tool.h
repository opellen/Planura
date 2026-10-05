#pragma once

#include <array>
#include <optional>
#include <vector>

#include <QPointF>

#include <geo/entity.h>
#include <geo/pick.h>
#include <geo/scene.h>
#include <geo/scene_ops.h>
#include <geo/vec3.h>

#include "agent/events.h"
#include "tool.h"

namespace plnr::tools {

// Flip: three axis-aligned mirror planes over the selection's bbox (X red, Y green, Z blue). Click mirrors
// about a plane; drag translates it along its normal (offset_ persists until the bbox changes). Nearest
// ray hit wins. Arrows (Left/Right/Up = green/red/blue) select a plane, Enter commits (MCP affordance,
// not a verified the reference modeler shortcut). Ctrl toggles copy mode on a real KeyPress edge; VCB Array retype
// only routes to the post-copy array window.
class FlipTool : public Tool {
public:
    void onActivate(ToolContext& ctx) override;
    // MUST clear the preview overlay (Tool contract).
    void onDeactivate(ToolContext& ctx) override;
    void onPointerMove(ToolContext& ctx, const PointerEvent& e) override;
    void onPointerDown(ToolContext& ctx, const PointerEvent& e) override;
    void onPointerUp(ToolContext& ctx, const PointerEvent& e) override;
    void onKeyDown(ToolContext& ctx, int key, bool ctrl) override;
    void onVcbCommit(ToolContext& ctx, const VcbValue& value) override;

private:
    enum class Stage { Idle, Armed };

    struct Bbox {
        geo::Vec3 min, max;
        bool valid{};
    };

    // One of the three cardinal planes for the current bbox_/offset_; computed fresh (computePlane), never cached.
    struct PlaneGeom {
        geo::Vec3 normal;              // world axis i, unit length
        geo::Vec3 axisU, axisV;        // the other two world axes -- the rectangle's in-plane directions
        geo::Vec3 center;              // bbox center + offset_[i] along normal
        double halfU{}, halfV{};       // rectangle half-extents along axisU/axisV
        PreviewColor color{};          // kAxisRedColor/kAxisGreenColor/kAxisBlueColor per axis
    };

    // A press in progress; nullopt when the left button isn't held. pos + dragging separate click (commit) from drag (translate).
    struct PressState {
        int plane{-1};        // 0=X, 1=Y, 2=Z -- see computePlane
        QPointF pos;           // press-time screen position (click-vs-drag threshold)
        double startOffset{};  // offset_[plane] at press time
        bool dragging{false};  // true once past the screen-pixel move threshold
    };

    // Rebuilds bbox_ from ctx.selection()'s closure; offset_ resets only when the bbox changes. False (to Idle) on an empty/vertex-less selection.
    bool refreshFromSelection(ToolContext& ctx);

    geo::Vec3 bboxCenter() const;
    double axisHalfExtent(int axis) const;  // bbox_'s own half-extent along world axis `axis`

    // Rectangle margin beyond the bbox: a fraction of the diagonal, floored so a degenerate selection still gets a clickable plane.
    double margin() const;

    // Resolves plane axisIdx (0=X/1=Y/2=Z) against the current bbox_/offset_.
    PlaneGeom computePlane(int axisIdx) const;

    // Nearest plane under e.ray hit inside its rectangle; -1 if none (or bbox_ invalid).
    int pickPlane(const PointerEvent& e) const;

    // Highlighted plane: grabbed (press_) > arrow key (selectedPlane_) > hover (hoverPlane_); -1 = none.
    int activePlane() const;

    // kIdleHint plus a live copy-mode/active-plane readout, pollable via status().hint.
    std::string idleHintText() const;

    // Full three-plane wireframe preview. Empty if bbox_ isn't valid.
    std::vector<ToolContext::PreviewBatch> buildPreview() const;

    // Mirrors ctx.selection() about plane planeIdx; copies = copyMode_ ? 1 : 0. Empty selection = hint only. Does not reset offset_.
    void commitFlip(ToolContext& ctx, int planeIdx);

    Stage stage_ = Stage::Idle;
    Bbox bbox_;

    // Per-axis translation of that plane along its normal, relative to the bbox center. 0=X, 1=Y, 2=Z.
    std::array<double, 3> offset_{};

    std::optional<int> selectedPlane_;  // arrow-key override -- see the class comment
    int hoverPlane_ = -1;                // this pointer-move's ray-plane pick, Armed + not dragging only

    std::optional<PressState> press_;

    // True after an odd number of real Qt::Key_Control KeyPress toggles.
    bool copyMode_ = false;
};

}  // namespace plnr::tools
