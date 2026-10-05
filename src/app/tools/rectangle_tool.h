#pragma once

#include <optional>
#include <utility>

#include <geo/infer.h>
#include <geo/vec3.h>

#include "tool.h"

namespace plnr::tools {

// Rectangle: click one corner, click the diagonal corner to commit. Zero width/height second click is ignored;
// Escape cancels. Infers Square/Golden Section cues while dragging (snapProportion()). Also draws on
// non-ground planes: face-plane, stood-up-vertical, flat-ground.
class RectangleTool : public Tool {
public:
    void onActivate(ToolContext& ctx) override;
    void onDeactivate(ToolContext& ctx) override;
    void onPointerMove(ToolContext& ctx, const PointerEvent& e) override;
    void onPointerDown(ToolContext& ctx, const PointerEvent& e) override;
    void onKeyDown(ToolContext& ctx, int key, bool ctrl) override;

    // VCB commit (rubber-band only; no retro-edit window). Needs corner1_ + lastPlanePoint_: Dims2 commits |a| x |b| along the CURRENT plane's (lastU_, lastV_) from corner1_, signs following the drag quadrant. Scalar/else = "Invalid entry.".
    void onVcbCommit(ToolContext& ctx, const VcbValue& value) override;

private:
    // Plane the SECOND corner resolves against / commit path: Ground (requestAddRectangle) vs Face/Vertical (requestAddPolyline).
    enum class DrawPlane { Ground, Face, Vertical };

    // resolveFirstCorner()'s return: inf = first-corner candidate; onFace/normal meaningful only when inf.kind == OnFace (case A).
    struct FirstCornerHit {
        geo::Inference inf;
        bool onFace{};
        geo::Vec3 normal;
    };

    // Where the FIRST corner would land: raw inference kept AS-IS if OnFace (case A), else resolve()'s ground-only fallback (case C).
    FirstCornerHit resolveFirstCorner(ToolContext& ctx, const PointerEvent& e) const;

    // resolveCorner2()'s return: pos/u/v/plane of the candidate; valid is false if nothing resolved this move.
    struct Corner2Candidate {
        geo::Vec3 pos;
        geo::Vec3 u;
        geo::Vec3 v;
        DrawPlane plane{DrawPlane::Ground};
        bool valid{};
    };

    // Resolves the SECOND corner against corner1_'s plane (case A, fixed), or dynamically against ground (case C) or a standing-up plane (case B).
    Corner2Candidate resolveCorner2(ToolContext& ctx, const PointerEvent& e);

    // geo::infer() with the given anchor, WITHOUT resolve()'s ground fallback/flatten.
    geo::Inference resolveRaw(ToolContext& ctx, const PointerEvent& e, std::optional<geo::Vec3> anchor) const;

    // resolveRaw(e, nullopt) plus the ground-plane-only contract (groundFallback in the .cpp).
    geo::Inference resolve(ToolContext& ctx, const PointerEvent& e) const;

    // Resets to idle: no corner selected, hint/preview reset.
    void reset(ToolContext& ctx);

    // Proportion snapProportion() last fired; None when off any.
    enum class ProportionKind { None, Square, Golden };

    // snapProportion()'s return: corner2 is c2 verbatim (kind==None) or the snapped replacement.
    struct ProportionSnap {
        geo::Vec3 corner2;
        ProportionKind kind;
    };

    // Square/Golden Section snap: nudges corner2 onto a 1:1 or golden-ratio du/dv; Shift holds lastProportion_.
    ProportionSnap snapProportion(const geo::Vec3& corner1, const geo::Vec3& corner2, const geo::Vec3& u,
                                   const geo::Vec3& v, bool shiftHeld) const;

    std::optional<geo::Vec3> corner1_;

    // True once corner1_ is placed via an OnFace hit (plane fixed); false = Ground base, decided fresh each call.
    bool corner1OnFace_ = false;
    geo::Vec3 faceNormal_{0.0, 0.0, 1.0};
    geo::Vec3 faceU_{1.0, 0.0, 0.0};
    geo::Vec3 faceV_{0.0, 1.0, 0.0};

    // Case-B hysteresis: set once inference leaves corner1_'s height, cleared on return ("ride up, then sideways").
    bool verticalLatched_ = false;

    // Last resolveCorner2() candidate's pos/basis/plane, for onVcbCommit's Dims2 sign (like the shape tools' lastGround_).
    std::optional<geo::Vec3> lastPlanePoint_;
    geo::Vec3 lastU_{1.0, 0.0, 0.0};
    geo::Vec3 lastV_{0.0, 1.0, 0.0};
    DrawPlane lastDrawPlane_ = DrawPlane::Ground;

    // Shift-lock's memory of the last proportion; cleared in onActivate/onDeactivate/reset().
    ProportionKind lastProportion_ = ProportionKind::None;
};

}  // namespace plnr::tools
