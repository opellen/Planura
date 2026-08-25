#pragma once

#include <array>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <geo/vec3.h>

namespace plnr::geo {

struct Transform;  // geo/scene.h

struct ModelJournal;  // geo/journal.h

// Half-edge mesh kernel. next/prev cycles exist only inside face loops; "wire"
// half-edges (not part of any face) keep face/next/prev at kInvalidId.

using Id = std::uint64_t;  // 0 = invalid
inline constexpr Id kInvalidId = 0;

struct LoopCandidate;  // geo/loop_finder.h

struct Vertex {
    Id id{};
    Vec3 pos;
    std::vector<Id> outgoing;  // half-edge ids originating at this vertex
};

struct HalfEdge {
    Id id{};
    Id origin{};
    Id twin{};
    Id next{};
    Id prev{};
    Id face{};
    Id edge{};
};

struct Edge {
    Id id{};
    std::array<Id, 2> halfEdges{};
};

struct Face {
    Id id{};
    Id halfEdge{};
    Vec3 normal;
};

struct AddEdgeResult {
    Id edge{};
    std::vector<Id> newFaces;
    bool created{};
};

struct ExtrudeResult {
    Id capFace{kInvalidId};
    std::vector<Id> sideFaces;
    std::vector<Id> newVertices;
    bool ok{};
};

struct SplitEdgeResult {
    Id newVertex{kInvalidId};
    Id edgeA{kInvalidId};
    Id edgeB{kInvalidId};
    bool ok{};
};

struct SplitFaceResult {
    Id faceA{kInvalidId};
    Id faceB{kInvalidId};
    bool ok{};
};

class Model {
public:
    // Merges each endpoint into an existing vertex within kMergeTol (nearest wins). A zero-length
    // edge after merging gives {kInvalidId, {}, false}; an already-connected pair gives
    // {existingEdgeId, {}, false}. detectFaces=false skips only the closing-loop auto-face step.
    AddEdgeResult addEdge(Vec3 a, Vec3 b, bool detectFaces = true);

    // Removes an edge and both halves; any face using either half is dissolved first (its loop
    // reverts to wire). Endpoint vertices left with no outgoing half-edges are GC'd. False if unknown.
    bool removeEdge(Id edgeId);

    // Removes faceId only: its boundary loop reverts to wire, edges and vertices stand (erasing an
    // EDGE takes bordering faces with it; erasing a FACE does not). False for an unknown id.
    bool removeFace(Id faceId);

    // Splits edgeId at M projected onto its own segment; face loops are re-chained in place
    // (id-preserving splice) and M reuses a vertex within kMergeTol. {.ok=false}, no mutation:
    // unknown edgeId, point within kMergeTol of an endpoint, or farther than kMergeTol from it.
    SplitEdgeResult splitEdge(Id edgeId, Vec3 point);

    // Cuts faceId in two along chordEdgeId, a wire edge joining two of its loop vertices; faceId is
    // dissolved and both halves restated with its original normal. {.ok=false} before any mutation:
    // unknown ids, chord not wire on both sides, endpoints not both on the loop or adjacent in it,
    // or a resulting half with fewer than 3 vertices.
    SplitFaceResult splitFaceByChord(Id faceId, Id chordEdgeId);

    const std::unordered_map<Id, Vertex>& vertices() const;
    const std::unordered_map<Id, Edge>& edges() const;
    const std::unordered_map<Id, HalfEdge>& halfEdges() const;
    const std::unordered_map<Id, Face>& faces() const;

    // Nearest vertex within tol, or nullptr if none qualifies.
    const Vertex* findVertex(Vec3 pos, double tol = kMergeTol) const;

    const Vertex* vertex(Id id) const;
    const Edge* edge(Id id) const;
    const HalfEdge* halfEdge(Id id) const;
    const Face* face(Id id) const;

    // Ordered vertex ids around a face's half-edge cycle (empty if unknown id).
    std::vector<Id> faceVertexLoop(Id faceId) const;

    // Extrudes a face along its normal by distance (negative = inward); new vertices are never
    // merged with existing geometry. The original face is recreated with reversed winding unless a
    // boundary edge was already shared with a neighbor (the new side face takes that winding).
    // Unknown faceId or |distance| < kMergeTol -> {.ok=false}, no mutation.
    ExtrudeResult extrudeFace(Id faceId, double distance);

    // Moves the given vertices by delta and recomputes the Newell normal of every incident face.
    // Planarity is not re-validated and coincident vertices are not merged. False (no mutation) if
    // any id is unknown or |delta| < kEps.
    bool translateVertices(const std::vector<Id>& vertexIds, Vec3 delta);

    // Affine sibling of translateVertices: moves vertices to xf.apply(pos), same normal recompute
    // and same non-validation. False (no mutation) if any id is unknown, or xf moves every listed
    // vertex by less than kEps.
    bool transformVertices(const std::vector<Id>& vertexIds, const Transform& xf);

    // Creates a face on an existing closed vertex loop (every consecutive pair, wrap included, must
    // already be connected by an edge). Tries the loop's own winding first, the twin side otherwise;
    // kInvalidId (no mutation) for < 3 vertices, a missing edge, or both windings already claimed.
    Id addFaceOnLoop(const std::vector<Id>& vertexLoop, Vec3 normal);

    // -- Journal --------------------------------------------------------------
    // Attaches (nullptr detaches) a non-owning observer notified of every record create/erase/modify
    // below, from ANY mutation path including restore* and *ForRollback. Contract: geo/journal.h.
    void setJournal(ModelJournal* journal);

    // -- Restore APIs -- rebuild a model from serialized records under their ORIGINAL ids (file
    // loader, undo/redo snapshot restore). None of them merge, dedupe, or auto-detect faces; replay
    // verbatim in order: restoreNextId once, then every restoreVertex, restoreEdge, restoreFace.

    // MUST be the first restore* call. Sets nextId_ = nextId; the loader passes max(id to restore)
    // + 1 so half-edge ids minted inside restoreEdge cannot collide. False (no mutation) if nextId
    // is below the current nextId_.
    bool restoreNextId(Id nextId);

    // Inserts Vertex{id, pos, {}} verbatim -- no merge, no kMergeTol coincidence check. False (no
    // mutation) if id == kInvalidId, id >= nextId_, or id is already used by ANY record kind.
    bool restoreVertex(Id id, Vec3 pos);

    // Inserts Edge{id} plus its two twin wire half-edges, registered as outgoing on v0 and v1. No
    // merge, no duplicate-edge check, no loop auto-detection. False (no mutation) if id is
    // invalid/already used, v0 == v1, or either vertex is unknown.
    bool restoreEdge(Id id, Id v0, Id v1);

    // Creates Face{id} on a closed vertex loop with STRICT winding: each pair's half-edge is claimed
    // in exactly the stated direction, no twin fallback -- the saved winding is authoritative.
    // Normal is the Newell normal of the stored order. False (no mutation) if id is invalid/used,
    // the loop has < 3 vertices, a loop vertex is unknown, or a half-edge is missing or claimed.
    bool restoreFace(Id id, const std::vector<Id>& vertexLoop);

    // -- Rollback primitives -- reverse twins of the restore APIs, for the TransactionManager's undo
    // path: erase a record (or reset a vertex position) back to a ModelJournal before-image. Guards
    // validate before any mutation; a rejected call leaves the model untouched. They notify too.

    // Erases Vertex{id} only if it has no outgoing half-edges; never GCs on its own. False (no
    // mutation) if id is unknown or still has outgoing half-edges.
    bool eraseVertexForRollback(Id id);

    // Erases Edge{id} (both halves) only if BOTH are wire -- roll back a referencing face first.
    // Unlike removeEdge it does NOT garbage-collect orphaned endpoint vertices. False (no mutation)
    // if id is unknown or either half-edge is claimed by a face.
    bool eraseEdgeForRollback(Id id);

    // Erases Face{id} by the same dissolve surgery removeEdge/splitFaceByChord use (half-edges
    // revert to wire, face record erased). False if id is unknown.
    bool eraseFaceForRollback(Id id);

    // Sets vertex id's position to pos with no merge, snapping, or did-it-move guard (a rollback
    // must be exact) and recomputes incident face normals. False (no mutation) if id is unknown.
    bool setVertexPosForRollback(Id id, Vec3 pos);

private:
    Id nextId_ = 1;  // shared counter across all record kinds; ids never reused

    std::unordered_map<Id, Vertex> vertices_;
    std::unordered_map<Id, Edge> edges_;
    std::unordered_map<Id, HalfEdge> halfEdges_;
    std::unordered_map<Id, Face> faces_;

    ModelJournal* journal_ = nullptr;  // non-owning, nullable

    Id allocId();
    // True if id is valid, below nextId_, and unused across all four record maps.
    bool isRestorableId(Id id) const;

    // -- Journal choke points -------------------------------------------------
    // Every record insert/erase/modify funnels through these (plus commitFace/dissolveFace for face
    // records); splitEdge's in-place loop re-chain calls notifyFaceLoopModified directly instead.

    // Inserts Vertex{id, pos, {}} and notifies journal_->vertexCreated. No merge/coincidence check.
    Vertex& insertVertexRecord(Id id, Vec3 pos);
    // Erases Vertex{id} and notifies journal_->vertexDeleted with the captured pos. Caller
    // guarantees it exists with no outgoing half-edges left.
    void eraseVertexRecord(Id id);
    // Sets vertices_.at(id).pos and notifies journal_->vertexMoved(id, before, after). Does NOT
    // recompute incident face normals -- callers do that afterward.
    void setVertexPosRecord(Id id, Vec3 pos);
    // Inserts Edge{id, {he0, he1}} (halves already built by the caller) and notifies
    // journal_->edgeCreated(id, v0, v1). v0/v1 are explicit because splitEdge's non-wire half-edge
    // origins alone would not disambiguate which vertex is "v0".
    Id insertEdgeRecord(Id id, Id he0, Id he1, Id v0, Id v1);
    // Erases both halves plus Edge{id} and notifies journal_->edgeDeleted(id, v0, v1), with v0/v1
    // captured from the halves' origins pre-erase. Caller owns vertex-outgoing-list bookkeeping.
    void eraseEdgeRecord(Id id, Id he0, Id he1);
    // Notify-only helper for splitEdge's in-place face-loop re-chain. No-op if journal_ is null.
    void notifyFaceLoopModified(Id id, const std::vector<Id>& loopBefore, const std::vector<Id>& loopAfter);

    Id resolveOrCreateVertex(Vec3 pos, bool& created);
    // Erases a vertex addEdge created but left unused; a rejected call must leave the model intact.
    void discardIfOrphan(Id vertexId, bool created);
    Id findExistingEdge(Id vA, Id vB) const;
    // Raw record tail of addEdge: allocates the Edge plus its two twin wire half-edges, registered
    // as outgoing on vA/vB. No merge/duplicate checks, no loop detection.
    Id createEdgeRecord(Id vA, Id vB);
    // Same, under a caller-supplied edgeId (restoreEdge's saved id).
    Id createEdgeRecordWithId(Id edgeId, Id vA, Id vB);
    // Reverts every half-edge of faceId to wire state (face/next/prev = kInvalidId) and erases Face.
    void dissolveFace(Id faceId);
    // Claims the half-edges backing vertexLoop (caller guarantees each consecutive pair is
    // connected) and chains them into a new Face. Tries the loop's own winding first, then the twin
    // side with -normal. kInvalidId, no mutation, if both windings are already claimed.
    Id createFaceOnLoop(const std::vector<Id>& vertexLoop, Vec3 normal);
    // Resolves vertexLoop's half-edges in its own stated direction, retrying the twin winding only
    // when allowTwinFallback; chosenOut gets them reindexed into ready-to-chain order, normalOut the
    // correctly-signed normal. False, nothing touched, if a pair is unconnected or already claimed.
    bool resolveLoopWinding(const std::vector<Id>& vertexLoop, Vec3 normal, bool allowTwinFallback,
                             std::vector<Id>& chosenOut, Vec3& normalOut) const;
    // Chains chosen's half-edges (already resolved free and reindexed by resolveLoopWinding) into
    // Face{faceId, chosen[0], faceNormal}. Pure commit, no checks, always succeeds.
    Id commitFace(Id faceId, const std::vector<Id>& chosen, Vec3 faceNormal);
    // createFaceOnLoop for a detected LoopCandidate.
    Id createFace(const LoopCandidate& loop);
    // Recomputes the Newell normal of every face whose loop contains a vertex in movedIds; scans all
    // faces rather than walking incident half-edges. Shared tail of translate/transformVertices.
    void recomputeIncidentFaceNormals(const std::unordered_set<Id>& movedIds);
};

}  // namespace plnr::geo
