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
    // Merges each endpoint into the nearest vertex within kMergeTol. Zero-length after merging ->
    // {kInvalidId, {}, false}; already connected -> {existingEdgeId, {}, false}.
    // detectFaces=false skips only the auto-face step.
    AddEdgeResult addEdge(Vec3 a, Vec3 b, bool detectFaces = true);

    // Dissolves any face using the edge first, then GCs endpoints left with no outgoing half-edges.
    bool removeEdge(Id edgeId);

    // Removes the face only; its loop reverts to wire and edges/vertices stay.
    bool removeFace(Id faceId);

    // Splits at point projected onto the segment; face loops are re-chained in place (face ids
    // survive) and the new vertex reuses one within kMergeTol. Fails without mutation within
    // kMergeTol of an endpoint or more than kMergeTol off the segment.
    SplitEdgeResult splitEdge(Id edgeId, Vec3 point);

    // Cuts the face along a wire chord joining two non-adjacent loop vertices; both halves keep the
    // original normal. Fails before any mutation on bad ids, a bad chord, or a half with < 3 vertices.
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

    // Extrudes along the face normal (negative = inward); new vertices never merge. The original
    // face is recreated reversed unless a boundary edge was already shared (the side face takes that
    // winding). Fails without mutation on |distance| < kMergeTol.
    ExtrudeResult extrudeFace(Id faceId, double distance);

    // Recomputes incident face normals; planarity is not re-validated and nothing merges.
    // Fails without mutation on an unknown id or |delta| < kEps.
    bool translateVertices(const std::vector<Id>& vertexIds, Vec3 delta);

    // translateVertices via xf.apply(pos); also fails if xf moves every vertex by less than kEps.
    bool transformVertices(const std::vector<Id>& vertexIds, const Transform& xf);

    // Every consecutive pair (wrap included) must already be an edge. Tries the loop's own winding,
    // then the twin side; kInvalidId (no mutation) if both are claimed.
    Id addFaceOnLoop(const std::vector<Id>& vertexLoop, Vec3 normal);

    // -- Journal --------------------------------------------------------------
    // Non-owning, nullable; notified from every mutation path, restore*/*ForRollback included.
    void setJournal(ModelJournal* journal);

    // -- Restore APIs: rebuild from serialized records under their original ids, verbatim (no
    // merge, dedupe or face detection). Order: restoreNextId once, then vertices, edges, faces.

    // Must come first: pass max(restored id) + 1 so half-edge ids minted by restoreEdge can't
    // collide. Fails if below the current nextId_.
    bool restoreNextId(Id nextId);

    // Fails on kInvalidId, id >= nextId_, or an id used by any record kind.
    bool restoreVertex(Id id, Vec3 pos);

    // Fails on an unusable id, v0 == v1, or an unknown vertex.
    bool restoreEdge(Id id, Id v0, Id v1);

    // Strict winding: each half-edge is claimed in the stated direction, no twin fallback. Normal =
    // Newell normal of the stored order. Fails without mutation on a missing or claimed half-edge.
    bool restoreFace(Id id, const std::vector<Id>& vertexLoop);

    // -- Rollback primitives: reverse the restore APIs for undo. Guards run before any mutation;
    // these notify the journal too.

    // Only if it has no outgoing half-edges; never GCs.
    bool eraseVertexForRollback(Id id);

    // Only if both halves are wire (roll back faces first); unlike removeEdge, never GCs endpoints.
    bool eraseEdgeForRollback(Id id);

    bool eraseFaceForRollback(Id id);

    // Exact: no merge, snap or did-it-move guard. Recomputes incident face normals.
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
    // Every record insert/erase/modify goes through these (faces via commitFace/dissolveFace);
    // splitEdge's in-place re-chain calls notifyFaceLoopModified.

    // No merge check.
    Vertex& insertVertexRecord(Id id, Vec3 pos);
    // Caller guarantees the vertex has no outgoing half-edges left.
    void eraseVertexRecord(Id id);
    // Does NOT recompute incident face normals; callers do.
    void setVertexPosRecord(Id id, Vec3 pos);
    // Halves are pre-built by the caller; v0/v1 are explicit because splitEdge's half-edge origins
    // can't tell which one is v0.
    Id insertEdgeRecord(Id id, Id he0, Id he1, Id v0, Id v1);
    // Caller owns the vertices' outgoing-list bookkeeping.
    void eraseEdgeRecord(Id id, Id he0, Id he1);
    void notifyFaceLoopModified(Id id, const std::vector<Id>& loopBefore, const std::vector<Id>& loopAfter);

    Id resolveOrCreateVertex(Vec3 pos, bool& created);
    // Erases a vertex addEdge created but left unused; a rejected call must leave the model intact.
    void discardIfOrphan(Id vertexId, bool created);
    Id findExistingEdge(Id vA, Id vB) const;
    // Raw record tail of addEdge: Edge plus two wire half-edges, no checks or loop detection.
    Id createEdgeRecord(Id vA, Id vB);
    Id createEdgeRecordWithId(Id edgeId, Id vA, Id vB);
    // Reverts the face's half-edges to wire and erases the Face.
    void dissolveFace(Id faceId);
    // Caller guarantees each pair is connected. Tries the loop's own winding, then the twin side
    // with -normal; kInvalidId (no mutation) if both are claimed.
    Id createFaceOnLoop(const std::vector<Id>& vertexLoop, Vec3 normal);
    // Resolves the loop's half-edges into chain order (twin winding only if allowTwinFallback), with
    // the correctly signed normal. False, nothing touched, if a pair is unconnected or claimed.
    bool resolveLoopWinding(const std::vector<Id>& vertexLoop, Vec3 normal, bool allowTwinFallback,
                             std::vector<Id>& chosenOut, Vec3& normalOut) const;
    // Pure commit of resolveLoopWinding's output; no checks.
    Id commitFace(Id faceId, const std::vector<Id>& chosen, Vec3 faceNormal);
    Id createFace(const LoopCandidate& loop);
    // Scans all faces rather than walking incident half-edges.
    void recomputeIncidentFaceNormals(const std::unordered_set<Id>& movedIds);
};

}  // namespace plnr::geo
