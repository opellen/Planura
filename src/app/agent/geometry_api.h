#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include <geo/entity.h>
#include <geo/model.h>
#include <geo/scene.h>
#include <geo/vec3.h>
#include <ordo/core/agent.h>

#include "agent/events.h"

namespace plnr::agent {

inline constexpr std::string_view kGeometryApiName = "geometry";

// Owns the Scene (root Definition plus nested group/component Definitions and their Instances).
// Dispatches events::GeometryChanged only when a call actually changed the model.
class GeometryApi : public ordo::core::Agent {
public:
    GeometryApi();

    // Adds an edge between a and b (merging near-coincident endpoints), then runs runSplittingPass().
    // Fires GeometryChanged only if an edge/face was created.
    // If the pass splits the new edge, result.edge/newFaces may not resolve afterward.
    geo::AddEdgeResult addEdge(geo::Vec3 a, geo::Vec3 b);

    // Removes edgeId (and dissolves any face referencing it). Sends GeometryChanged only if removed.
    bool removeEdge(geo::Id id);

    // Bulk erase for the live selection. Edge dissolves its face; Face removal is face-only (edges
    // survive); Instance is removed at the root; Vertex refs are skipped (no standalone delete).
    // Unknown refs ignored. ONE GeometryChanged for the whole batch; no-op if nothing was removed.
    bool removeEntities(const std::vector<events::EntityRef>& refs);

    // Extrudes faceId along its normal by distance (negative = inward). Sends GeometryChanged only if result.ok.
    geo::ExtrudeResult extrudeFace(geo::Id faceId, double distance);

    // Moves entity (kind, id) by delta. An Instance target moves the WHOLE instance (not in-place);
    // other kinds move their resolved vertices. |delta| < geo::kEps is a no-op.
    bool moveEntity(geo::EntityKind kind, geo::Id id, geo::Vec3 delta);

    // Ground-plane axis-aligned rectangle from two diagonal corners (z forced 0).
    // Degenerate spans (< kMergeTol) are rejected: false, no mutation, no event.
    bool addRectangle(geo::Vec3 c1, geo::Vec3 c2);

    // Adds an open/closed polyline through points; closed adds a final edge when points.size() >= 3.
    // points.size() < 2 is a no-op. Records the created edges as the retro-edit window for
    // replaceLastPolyline (cleared on a no-op call).
    bool addPolyline(const std::vector<geo::Vec3>& points, bool closed);

    // Retypes the just-drawn polyline in place (the reference modeler VCB retro-edit). Requires an open window
    // (see clearLastPolylineOp()); otherwise a no-op. Replaces the previous call's edges and keeps
    // the window open for a further retype.
    bool replaceLastPolyline(const std::vector<geo::Vec3>& points, bool closed);

    // Hides/unhides every ref (any EntityKind; not tracked by geo::Model). Unknown refs ignored.
    // Sends GeometryChanged once iff the hidden set actually changed.
    bool setHidden(const std::vector<events::EntityRef>& refs, bool hidden);

    // Unhides every currently hidden entity. Sends GeometryChanged only if hidden_ was non-empty.
    bool unhideAll();

    bool isHidden(events::EntityRef ref) const;
    const std::unordered_set<events::EntityRef>& hidden() const;

    // Make Group / Make Component: builds the closure over refs into a new Definition (name auto-fills
    // "Group N"/"Component N") and places one Instance in the root. Empty closure is a no-op (kInvalidId).
    // The moved entities' ids do NOT survive.
    geo::Id makeGroup(const std::vector<events::EntityRef>& refs, bool asComponent, std::string name);

    // Explode -- the reverse of makeGroup. instanceId must be a direct child of the root; unknown id
    // is a no-op. The definition itself is left registered (never garbage-collected).
    bool explode(geo::Id instanceId);

    // Builds extruded glyph geometry from outlines (closed XY polygons, z=0, relative to origin) as
    // a new component instance. kInvalidId, no-op if outlines is empty or every outline has < 3 points.
    // Extrudes by `extrusion` only when > kMergeTol.
    geo::Id add3dText(const std::string& name, const std::vector<std::vector<geo::Vec3>>& outlines, double extrusion,
                       geo::Vec3 origin);

    // Applies spec to the selection closure over refs. Empty closure is a no-op. copies == 0
    // transforms in place; copies == N >= 1 leaves originals untouched and lays down N copies,
    // recorded as the array retro-edit window below.
    bool transformEntities(const std::vector<events::EntityRef>& refs, const events::TransformSpec& spec, int copies);

    // The array-copy retro-edit window (the reference modeler: type "*N"/"/N" in the VCB after a copy-drag).
    // Requires an open window and n >= 1, else no-op. Times replays at factors 1..n, Divide at
    // 1/n..n/n; the window stays open for a further retype.
    bool applyArrayTimes(int n);
    bool applyArrayDivide(int n);

    // Sweeps profileFaceId's vertex loop along pathPoints and stitches the result into the model.
    // No-op: unknown faceId, or geo::sweep rejects the profile/path.
    bool followMe(geo::Id profileFaceId, const std::vector<geo::Vec3>& pathPoints, bool closedPath);

    // Splits edgeId into n equal-length segments (n is a SEGMENT COUNT, not a split count).
    // Unlike addEdge/addPolyline, does NOT run runSplittingPass. Guards: n < 2, unknown edgeId -> no-op.
    bool divideEdge(geo::Id edgeId, int n);

    // Builds one new component Definition from an already-parsed OBJ mesh via the restore APIs
    // (preserving n-gon faces verbatim), plus one root Instance. name empty auto-names it.
    // kInvalidId if vertices is empty or every face has < 3 entries.
    geo::Id importMesh(std::string name, const std::vector<geo::Vec3>& vertices,
                        const std::vector<std::vector<std::size_t>>& faces);

    // Sets a root-level instance's transform directly (unlike moveEntity's Instance case, which
    // only composes a translation). No-op if instanceId isn't a direct child of the root.
    bool setInstanceTransform(geo::Id instanceId, const geo::Transform& transform);

    // -- Solid tools -------------------------------------------------------

    // One merged result face's provenance: which SOURCE face (by id) it descends from, for the
    // Command's own material carry-over pass (MaterialRepository isn't reachable from this Agent).
    struct SolidOpFaceProvenance {
        geo::Id newFaceId{};     // Face id in the NEW result Definition's own Model
        geo::Id sourceFaceId{};  // the ORIGINAL operand Definition's own Model face id it descends from
    };

    // Outcome of applySolidOp below.
    struct SolidOpResult {
        bool ok{};
        std::string hint;                             // meaningful only when ok == false
        std::vector<geo::Id> newInstanceIds;           // root Instances created (materialized results)
        std::vector<SolidOpFaceProvenance> provenance;  // across every newInstanceIds' own Definition
    };

    // Runs a CSG boolean/split over root Instances named by instanceIds, in CLICK ORDER (Subtract/
    // Trim: first id = cutter, last = target). Any arity/solid-ness/overlap violation returns
    // {ok=false, hint}, no-op. Removes the consumed operands; Trim keeps the cutter.
    SolidOpResult applySolidOp(events::SolidOp op, const std::vector<geo::Id>& instanceIds);

    const geo::Model& model() const;

    // Read accessor for the whole scene graph.
    const geo::Scene& scene() const;

    // Resolves the Definition that path names, walking from the root through its instance ids.
    // Returns nullptr for an invalid path (never falls back to root).
    const geo::Definition* contextDefinition(const std::vector<geo::Id>& path) const;

    // Convenience overload for the root context (empty path); always resolves.
    const geo::Definition* contextDefinition() const;

    // Read accessor for the Model that path's editing context names; nullptr if path is invalid.
    // Mutators (addEdge, etc.) still target root only; context-local drawing is deferred.
    const geo::Model* contextModel(const std::vector<geo::Id>& path) const;

    // -- Restore API -----------------------------------------------------
    // File loader / snapshot restore only: plain data manipulation, no notification.

    // Resets this agent to a fresh empty state: scene_ reset, hidden_ cleared, both windows closed.
    void clearForRestore();

    // Replaces scene_ wholesale with a fully-built scene. Loader-only: bypasses every normal
    // mutator's event/window bookkeeping on purpose.
    void adoptScene(geo::Scene&& scene);

    // Replaces hidden_ wholesale with refs (need not resolve in the just-adopted scene).
    void restoreHidden(std::vector<events::EntityRef> refs);

    // -- Mutable scene access ---------------------------------------------
    // TransactionManager-only: attaches/detaches journals and resolves undo/redo deltas.
    // Never call this from anywhere else.
    geo::Scene& sceneForUndoRollback();

private:
    // Splits newEdgeIds against existing/new geometry in 3 passes (endpoint-on-edge, edge-cross,
    // face-chord); returns the surviving fragment ids.
    std::vector<geo::Id> runSplittingPass(std::vector<geo::Id> newEdgeIds);

    // Drops any hidden_ entry whose (kind, id) no longer resolves, so hidden_ never accumulates stale refs.
    void pruneHiddenDeadRefs();

    // Thin accessor for scene_.root()'s Model.
    geo::Model& rootModel();
    const geo::Model& rootModel() const;

    // Closes the replaceLastPolyline retro-edit window. Not called by setHidden/unhideAll
    // (visibility isn't a geometry mutation).
    void clearLastPolylineOp();

    // Shared tail of applyArrayTimes/applyArrayDivide: divide selects the k/n factor formula,
    // false selects the 1..n (Times) formula.
    bool retypeArray(int n, bool divide);

    // Closes the array-copy retro-edit window (arrayOp_); independent of the polyline window.
    void clearArrayOp();

    geo::Scene scene_;
    std::unordered_set<events::EntityRef> hidden_;

    // Edge ids the most recent addPolyline/replaceLastPolyline call created.
    // hasLastPolylineOp_ false means no open window; lastPolylineEdgeIds_ is meaningful only when true.
    std::vector<geo::Id> lastPolylineEdgeIds_;
    bool hasLastPolylineOp_ = false;

    // The array-copy retro-edit window's record: refs/spec as originally requested (never mutated
    // by a retype); copies/copyEdgeIds from the most recent call. nullopt means no open window.
    struct ArrayOp {
        std::vector<events::EntityRef> refs;
        events::TransformSpec spec;
        int copies{};
        std::vector<geo::Id> copyEdgeIds;
    };
    std::optional<ArrayOp> arrayOp_;
};

}  // namespace plnr::agent
