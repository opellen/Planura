#pragma once

#include <optional>
#include <vector>

#include <geo/entity.h>
#include <geo/pick.h>
#include <geo/vec3.h>

#include "agent/events.h"
#include "tool.h"

namespace plnr::tools {

// industry-standard Follow Me, preselect-path method only: select a connected
// edge path FIRST (with the Select tool), THEN activate Follow Me and
// click the profile face to sweep it along that path.

// Path qualification reuses OffsetTool's chain-ordering idiom: 2+ Edge
// refs forming a single connected simple path or cycle; unlike Offset,
// coplanarity is NOT required (a Follow Me path is legitimately 3D).

// Re-derived from ctx.selection() on every pointer event, so selecting a
// path while active picks it up on the next move. Clicking commits via
// requestFollowMe and the tool stays active with selection untouched, so
// a further sweep needs no re-activation. Hints/colors are UNVERIFIED.

// Escape is non-destructive (no drag/arm state) -- it just re-derives and
// re-shows the current hint.
class FollowMeTool : public Tool {
public:
    void onActivate(ToolContext& ctx) override;
    void onDeactivate(ToolContext& ctx) override;
    void onPointerMove(ToolContext& ctx, const PointerEvent& e) override;
    void onPointerDown(ToolContext& ctx, const PointerEvent& e) override;
    void onKeyDown(ToolContext& ctx, int key, bool ctrl) override;

private:
    // The ordered path this activation would sweep along. points is the
    // ordered vertex list (last point NOT repeated when closed).
    struct PathData {
        std::vector<geo::Vec3> points;
        bool closed{};
    };

    // Validates ctx.selection() as a qualifying path (see the class
    // comment). nullopt on any failure.
    std::optional<PathData> tryBuildSelectedPath(ToolContext& ctx) const;

    // Reads a face's vertex loop as world-space points, or empty if faceId
    // is unknown or the loop is inconsistent.
    static std::vector<geo::Vec3> facePoints(const geo::Model& model, geo::Id faceId);

    // Rebuilds currentPath_/hoveredFaceId_ from ctx.selection() and e's pick,
    // then pushes the preview + hint. Shared by onActivate (e null) and onPointerMove.
    void refresh(ToolContext& ctx, const PointerEvent* e);

    static constexpr const char* kNoPathHint = "Select the path edges first, then click the profile face.";
    static constexpr const char* kReadyHint = "Click the profile face to sweep it along the selected path.";
    static constexpr const char* kSweptHint = "Follow Me applied. Select a new path to sweep another face.";

    std::optional<PathData> currentPath_;
    geo::Id hoveredFaceId_ = geo::kInvalidId;
};

}  // namespace plnr::tools
