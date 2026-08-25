#pragma once

#include <optional>
#include <utility>

#include <geo/infer.h>
#include <geo/vec3.h>

#include "tool.h"

namespace plnr::tools {

// industry-standard rectangle: click to set one corner, move to preview a
// rectangle outline, click again on the diagonal corner to commit it.
// Degenerate second click (zero width/height) is ignored; Escape cancels
// the in-progress corner. Infers Square/Golden Section proportion cues
// while dragging (see snapProportion()). Draws on non-ground planes too --
// three cases (face-plane, stood-up-vertical, flat-ground)
class RectangleTool : public Tool {
public:
    void onActivate(ToolContext& ctx) override;
    void onDeactivate(ToolContext& ctx) override;
    void onPointerMove(ToolContext& ctx, const PointerEvent& e) override;
    void onPointerDown(ToolContext& ctx, const PointerEvent& e) override;
    void onKeyDown(ToolContext& ctx, int key, bool ctrl) override;

    // Handles a committed VCB entry (in-progress rubber-band only -- no
    // retro-edit window). Requires corner1_ + lastPlanePoint_: Dims2 commits
    // |a| x |b| along the CURRENT plane's (lastU_, lastV_) axes from corner1_,
    // each axis' sign following the drag quadrant. Scalar/else = "Invalid entry.".
    void onVcbCommit(ToolContext& ctx, const VcbValue& value) override;

private:
    // Which plane the SECOND corner resolves against / commit path: Ground
    // (requestAddRectangle) vs Face/Vertical (requestAddPolyline)
    enum class DrawPlane { Ground, Face, Vertical };

    // resolveFirstCorner()'s return: inf is the first-corner candidate;
    // onFace/normal are meaningful only when inf.kind == OnFace (case A).
    struct FirstCornerHit {
        geo::Inference inf;
        bool onFace{};
        geo::Vec3 normal;
    };

    // Resolves where the FIRST corner would land: raw inference, kept AS-IS
    // if OnFace (case A), else resolve()'s ground-only fallback (case C).
    FirstCornerHit resolveFirstCorner(ToolContext& ctx, const PointerEvent& e) const;

    // resolveCorner2()'s return: pos/u/v/plane for the second-corner
    // candidate; valid is false if nothing resolved this move.
    struct Corner2Candidate {
        geo::Vec3 pos;
        geo::Vec3 u;
        geo::Vec3 v;
        DrawPlane plane{DrawPlane::Ground};
        bool valid{};
    };

    // Resolves the SECOND corner against corner1_'s plane (case A, fixed),
    // or dynamically against ground (case C) or a standing-up plane (case B).
    Corner2Candidate resolveCorner2(ToolContext& ctx, const PointerEvent& e);

    // Resolves e via geo::infer() with the given anchor, WITHOUT resolve()'s
    // ground-plane fallback/flatten post-processing.
    geo::Inference resolveRaw(ToolContext& ctx, const PointerEvent& e, std::optional<geo::Vec3> anchor) const;

    // Resolves e via resolveRaw(e, nullopt), then enforces the ground-plane-
    // only contract (see groundFallback in the .cpp).
    geo::Inference resolve(ToolContext& ctx, const PointerEvent& e) const;

    // Resets to the idle state (no corner selected), hint/preview reset.
    void reset(ToolContext& ctx);

    // Which proportion (if any) snapProportion() last fired -- None when
    // off any recognized proportion.
    enum class ProportionKind { None, Square, Golden };

    // snapProportion()'s return: corner2 is c2 verbatim (kind==None) or the
    // snapped replacement; kind updates lastProportion_ for the next call.
    struct ProportionSnap {
        geo::Vec3 corner2;
        ProportionKind kind;
    };

    // the reference modeler Square/Golden Section proportion snap: nudges corner2 onto a
    // 1:1 or golden-ratio du/dv; Shift holds lastProportion_
    ProportionSnap snapProportion(const geo::Vec3& corner1, const geo::Vec3& corner2, const geo::Vec3& u,
                                   const geo::Vec3& v, bool shiftHeld) const;

    std::optional<geo::Vec3> corner1_;

    // True once corner1_ is placed via an OnFace hit -- plane stays fixed;
    // false is the Ground base, decided fresh each call.
    bool corner1OnFace_ = false;
    geo::Vec3 faceNormal_{0.0, 0.0, 1.0};
    geo::Vec3 faceU_{1.0, 0.0, 0.0};
    geo::Vec3 faceV_{0.0, 1.0, 0.0};

    // Case-B hysteresis: set once the inference leaves corner1_'s height,
    // cleared on return to that level -- "ride up, then move sideways".
    bool verticalLatched_ = false;

    // Most recent resolveCorner2() candidate's pos/basis/plane, for
    // onVcbCommit's Dims2 sign -- mirrors the shape tools' lastGround_.
    std::optional<geo::Vec3> lastPlanePoint_;
    geo::Vec3 lastU_{1.0, 0.0, 0.0};
    geo::Vec3 lastV_{0.0, 1.0, 0.0};
    DrawPlane lastDrawPlane_ = DrawPlane::Ground;

    // The proportion snapProportion() fired last call -- Shift-lock's
    // memory, cleared in onActivate/onDeactivate/reset().
    ProportionKind lastProportion_ = ProportionKind::None;
};

}  // namespace plnr::tools
