#include <geo/model.h>

#include <algorithm>
#include <unordered_set>

#include <geo/journal.h>
#include <geo/loop_finder.h>
#include <geo/scene.h>  // Transform, for transformVertices

namespace plnr::geo {

namespace {

// Newell normal for a (possibly non-convex) planar polygon, vertex positions in cycle order.
// Degenerate/collinear input (normal length below kEps) yields the zero vector.
Vec3 newellNormal(const std::vector<Vec3>& positions) {
    Vec3 sum{0.0, 0.0, 0.0};
    const std::size_t n = positions.size();
    for (std::size_t i = 0; i < n; ++i) {
        const Vec3& p1 = positions[i];
        const Vec3& p2 = positions[(i + 1) % n];
        sum.x += (p1.y - p2.y) * (p1.z + p2.z);
        sum.y += (p1.z - p2.z) * (p1.x + p2.x);
        sum.z += (p1.x - p2.x) * (p1.y + p2.y);
    }
    return normalized(sum);
}

}  // namespace

Id Model::allocId() {
    return nextId_++;
}

void Model::setJournal(ModelJournal* journal) {
    journal_ = journal;
}

// -- Journal choke points -- every record insert/erase/modify funnels through one of these.

Vertex& Model::insertVertexRecord(Id id, Vec3 pos) {
    Vertex v;
    v.id = id;
    v.pos = pos;
    auto [it, inserted] = vertices_.emplace(id, std::move(v));
    if (journal_) {
        journal_->vertexCreated(id, pos);
    }
    return it->second;
}

void Model::eraseVertexRecord(Id id) {
    const auto it = vertices_.find(id);
    if (it == vertices_.end()) {
        return;  // defensive: caller guarantees existence
    }
    const Vec3 posBefore = it->second.pos;
    vertices_.erase(it);
    if (journal_) {
        journal_->vertexDeleted(id, posBefore);
    }
}

void Model::setVertexPosRecord(Id id, Vec3 pos) {
    Vertex& v = vertices_.at(id);
    const Vec3 before = v.pos;
    v.pos = pos;
    if (journal_) {
        journal_->vertexMoved(id, before, pos);
    }
}

Id Model::insertEdgeRecord(Id id, Id he0, Id he1, Id v0, Id v1) {
    Edge edge;
    edge.id = id;
    edge.halfEdges = {he0, he1};
    edges_.emplace(id, edge);
    if (journal_) {
        journal_->edgeCreated(id, v0, v1);
    }
    return id;
}

void Model::eraseEdgeRecord(Id id, Id he0, Id he1) {
    // Capture endpoints from the halves' origin fields before either is erased.
    const Id v0 = halfEdges_.at(he0).origin;
    const Id v1 = halfEdges_.at(he1).origin;
    halfEdges_.erase(he0);
    halfEdges_.erase(he1);
    edges_.erase(id);
    if (journal_) {
        journal_->edgeDeleted(id, v0, v1);
    }
}

void Model::notifyFaceLoopModified(Id id, const std::vector<Id>& loopBefore, const std::vector<Id>& loopAfter) {
    if (journal_) {
        journal_->faceLoopModified(id, loopBefore, loopAfter);
    }
}

Id Model::resolveOrCreateVertex(Vec3 pos, bool& created) {
    if (const Vertex* existing = findVertex(pos)) {
        created = false;
        return existing->id;
    }
    const Id id = allocId();
    insertVertexRecord(id, pos);
    created = true;
    return id;
}

void Model::discardIfOrphan(Id vertexId, bool created) {
    if (!created) {
        return;
    }
    const auto it = vertices_.find(vertexId);
    if (it != vertices_.end() && it->second.outgoing.empty()) {
        eraseVertexRecord(vertexId);
    }
}

Id Model::findExistingEdge(Id vA, Id vB) const {
    const auto it = vertices_.find(vA);
    if (it == vertices_.end()) {
        return kInvalidId;
    }
    for (Id heId : it->second.outgoing) {
        const auto heIt = halfEdges_.find(heId);
        if (heIt == halfEdges_.end()) {
            continue;
        }
        const auto twinIt = halfEdges_.find(heIt->second.twin);
        if (twinIt != halfEdges_.end() && twinIt->second.origin == vB) {
            return heIt->second.edge;
        }
    }
    return kInvalidId;
}

AddEdgeResult Model::addEdge(Vec3 a, Vec3 b, bool detectFaces) {
    bool createdA = false;
    bool createdB = false;
    const Id vA = resolveOrCreateVertex(a, createdA);
    const Id vB = resolveOrCreateVertex(b, createdB);

    if (vA == vB || distance(vertices_.at(vA).pos, vertices_.at(vB).pos) < kMergeTol) {
        discardIfOrphan(vB, createdB);
        discardIfOrphan(vA, createdA);
        return {kInvalidId, {}, false};
    }

    if (const Id existing = findExistingEdge(vA, vB); existing != kInvalidId) {
        return {existing, {}, false};
    }

    const Id edgeId = createEdgeRecord(vA, vB);

    // Loop detection: at most one face per insertion -- closing several loops at once only gets the
    // shortest, the rest need a follow-up edge. Skipped when detectFaces=false.
    if (const auto loop = detectFaces ? findLoopForNewEdge(*this, edgeId) : std::nullopt) {
        if (const Id faceId = createFace(*loop); faceId != kInvalidId) {
            return {edgeId, {faceId}, true};
        }
    }
    return {edgeId, {}, true};
}

Id Model::createEdgeRecord(Id vA, Id vB) {
    return createEdgeRecordWithId(allocId(), vA, vB);
}

Id Model::createEdgeRecordWithId(Id edgeId, Id vA, Id vB) {
    const Id heAB = allocId();
    const Id heBA = allocId();

    HalfEdge ab;
    ab.id = heAB;
    ab.origin = vA;
    ab.twin = heBA;
    ab.face = kInvalidId;
    ab.next = kInvalidId;
    ab.prev = kInvalidId;
    ab.edge = edgeId;

    HalfEdge ba;
    ba.id = heBA;
    ba.origin = vB;
    ba.twin = heAB;
    ba.face = kInvalidId;
    ba.next = kInvalidId;
    ba.prev = kInvalidId;
    ba.edge = edgeId;

    halfEdges_.emplace(heAB, ab);
    halfEdges_.emplace(heBA, ba);

    vertices_.at(vA).outgoing.push_back(heAB);
    vertices_.at(vB).outgoing.push_back(heBA);

    return insertEdgeRecord(edgeId, heAB, heBA, vA, vB);
}

Id Model::createFaceOnLoop(const std::vector<Id>& vertexLoop, Vec3 normal) {
    if (vertexLoop.size() < 3) {
        return kInvalidId;
    }

    std::vector<Id> chosen;
    Vec3 faceNormal;
    if (!resolveLoopWinding(vertexLoop, normal, /*allowTwinFallback=*/true, chosen, faceNormal)) {
        return kInvalidId;  // no mutation -- missing edge, or both windings already claimed
    }

    // Id allocated only now the loop is known claimable, so a rejected call never burns an id.
    return commitFace(allocId(), chosen, faceNormal);
}

bool Model::resolveLoopWinding(const std::vector<Id>& vertexLoop, Vec3 normal, bool allowTwinFallback,
                                std::vector<Id>& chosenOut, Vec3& normalOut) const {
    const std::size_t n = vertexLoop.size();

    // Per consecutive pair: the forward half-edge (origin == the pair's first vertex) and its twin.
    std::vector<Id> forward(n);
    std::vector<Id> reversed(n);
    for (std::size_t i = 0; i < n; ++i) {
        const Id vA = vertexLoop[i];
        const Id vB = vertexLoop[(i + 1) % n];
        const Id edgeId = findExistingEdge(vA, vB);
        if (edgeId == kInvalidId) {
            return false;  // no connecting edge -- caller guarantees these exist (or a corrupt file)
        }
        const Edge& edge = edges_.at(edgeId);
        const Id h0 = edge.halfEdges[0];
        const Id h1 = edge.halfEdges[1];
        if (halfEdges_.at(h0).origin == vA) {
            forward[i] = h0;
            reversed[i] = h1;
        } else {
            forward[i] = h1;
            reversed[i] = h0;
        }
    }

    const auto allFree = [this](const std::vector<Id>& half) {
        return std::all_of(half.begin(), half.end(),
                            [this](Id id) { return halfEdges_.at(id).face == kInvalidId; });
    };

    if (allFree(forward)) {
        chosenOut = std::move(forward);
        normalOut = normal;
        return true;
    }
    if (!allowTwinFallback || !allFree(reversed)) {
        return false;  // required winding(s) already claimed -- no mutation
    }

    // Twin fallback: reindex into the same ready-to-chain order as the forward case (chosenOut[i]'s
    // next is chosenOut[i + 1]). reversed[i] runs vertexLoop[i + 1] -> vertexLoop[i], so walking the
    // cycle backwards visits reversed[n - 1], reversed[n - 2], ..., reversed[0] in order.
    chosenOut.resize(n);
    for (std::size_t i = 0; i < n; ++i) {
        chosenOut[i] = reversed[(n - 1 - i) % n];
    }
    // Reversing the backing half-edges reverses traversal, which flips the Newell normal's sign.
    normalOut = -normal;
    return true;
}

Id Model::commitFace(Id faceId, const std::vector<Id>& chosen, Vec3 faceNormal) {
    const std::size_t n = chosen.size();
    for (std::size_t i = 0; i < n; ++i) {
        HalfEdge& he = halfEdges_.at(chosen[i]);
        he.face = faceId;
        he.next = chosen[(i + 1) % n];
        he.prev = chosen[(i + n - 1) % n];
    }

    Face face;
    face.id = faceId;
    face.halfEdge = chosen[0];
    face.normal = faceNormal;
    faces_.emplace(faceId, face);

    // Journal choke point for every face creation. faceVertexLoop re-derives the loop from the
    // half-edge cycle just chained rather than trusting the caller's vertexLoop -- the twin
    // fallback's traversal order can differ.
    if (journal_) {
        journal_->faceCreated(faceId, faceVertexLoop(faceId));
    }

    return faceId;
}

Id Model::createFace(const LoopCandidate& loop) {
    return createFaceOnLoop(loop.vertexLoop, loop.normal);
}

void Model::dissolveFace(Id faceId) {
    // Journal choke point for every dissolve in the kernel: capture the loop BEFORE reverting any
    // half-edge to wire state.
    const std::vector<Id> loopBefore = faceVertexLoop(faceId);

    for (auto& [id, he] : halfEdges_) {
        if (he.face == faceId) {
            he.face = kInvalidId;
            he.next = kInvalidId;
            he.prev = kInvalidId;
        }
    }
    faces_.erase(faceId);

    if (journal_) {
        journal_->faceDeleted(faceId, loopBefore);
    }
}

bool Model::removeEdge(Id edgeId) {
    const auto edgeIt = edges_.find(edgeId);
    if (edgeIt == edges_.end()) {
        return false;
    }

    const std::array<Id, 2> halves = edgeIt->second.halfEdges;

    // Dissolve any face referencing either half: its half-edges revert to wire, the record is erased.
    for (Id heId : halves) {
        const auto heIt = halfEdges_.find(heId);
        if (heIt == halfEdges_.end()) {
            continue;
        }
        const Id faceId = heIt->second.face;
        if (faceId == kInvalidId) {
            continue;
        }
        dissolveFace(faceId);
    }

    // Capture each half's origin before erasing anything: edgeDeleted MUST fire before any
    // orphan-vertex vertexDeleted from the same call, so that a backward replay restores the
    // endpoints before the edge that needs them.
    const Id origin0 = halfEdges_.at(halves[0]).origin;
    const Id origin1 = halfEdges_.at(halves[1]).origin;

    eraseEdgeRecord(edgeId, halves[0], halves[1]);

    // Drop the halves from their origins' outgoing lists and GC vertices left with none; the
    // resulting vertexDeleted events land after edgeDeleted above, as required.
    const auto cleanupOrigin = [this](Id originId, Id heId) {
        const auto vIt = vertices_.find(originId);
        if (vIt == vertices_.end()) {
            return;
        }
        auto& outgoing = vIt->second.outgoing;
        outgoing.erase(std::remove(outgoing.begin(), outgoing.end(), heId), outgoing.end());
        if (outgoing.empty()) {
            eraseVertexRecord(originId);
        }
    };
    cleanupOrigin(origin0, halves[0]);
    cleanupOrigin(origin1, halves[1]);

    return true;
}

bool Model::removeFace(Id faceId) {
    if (faces_.find(faceId) == faces_.end()) {
        return false;
    }
    dissolveFace(faceId);
    return true;
}

SplitEdgeResult Model::splitEdge(Id edgeId, Vec3 point) {
    const auto edgeIt = edges_.find(edgeId);
    if (edgeIt == edges_.end()) {
        return {};  // ok=false, no mutation -- unknown edgeId
    }

    const Id h0 = edgeIt->second.halfEdges[0];
    const Id h1 = edgeIt->second.halfEdges[1];
    const Id vA = halfEdges_.at(h0).origin;
    const Id vB = halfEdges_.at(h1).origin;
    const Vec3 posA = vertices_.at(vA).pos;
    const Vec3 posB = vertices_.at(vB).pos;

    if (distance(point, posA) < kMergeTol || distance(point, posB) < kMergeTol) {
        return {};  // nothing to split -- point coincides with an existing endpoint
    }

    // Projection onto the FINITE segment (t clamped to [0, 1]), not the infinite line through it.
    const Vec3 dir = posB - posA;
    const double t = std::clamp(dot(point - posA, dir) / dot(dir, dir), 0.0, 1.0);
    const Vec3 proj = posA + dir * t;
    if (distance(point, proj) > kMergeTol) {
        return {};  // off-edge
    }

    // --- Guards passed: every step below commits to mutating the model. ---

    // Snapshot h0/h1's face/next/prev before either record is touched; both re-chains below read it.
    const HalfEdge oldH0 = halfEdges_.at(h0);
    const HalfEdge oldH1 = halfEdges_.at(h1);

    // The one kernel surgery that re-chains a face loop IN PLACE (the face id survives): capture the
    // before-loop here, the after-loop once the new half-edges are chained below, then report both
    // via notifyFaceLoopModified.
    const std::vector<Id> loopBefore0 = (oldH0.face != kInvalidId) ? faceVertexLoop(oldH0.face) : std::vector<Id>{};
    const std::vector<Id> loopBefore1 = (oldH1.face != kInvalidId) ? faceVertexLoop(oldH1.face) : std::vector<Id>{};

    // Reuse a vertex within kMergeTol of proj -- the weld a crossing split depends on. It can never
    // be vA/vB; the guard above already rejected any point within kMergeTol of an endpoint.
    const Vertex* reused = findVertex(proj);
    const Id mId = reused != nullptr ? reused->id : allocId();
    if (reused == nullptr) {
        insertVertexRecord(mId, proj);
    }

    const Id h0aId = allocId();  // A -> M
    const Id h0bId = allocId();  // M -> B
    const Id h1bId = allocId();  // B -> M
    const Id h1aId = allocId();  // M -> A
    const Id edgeAId = allocId();
    const Id edgeBId = allocId();

    HalfEdge h0a;
    h0a.id = h0aId;
    h0a.origin = vA;
    h0a.twin = h1aId;
    h0a.edge = edgeAId;
    h0a.face = oldH0.face;

    HalfEdge h0b;
    h0b.id = h0bId;
    h0b.origin = mId;
    h0b.twin = h1bId;
    h0b.edge = edgeBId;
    h0b.face = oldH0.face;

    HalfEdge h1b;
    h1b.id = h1bId;
    h1b.origin = vB;
    h1b.twin = h0bId;
    h1b.edge = edgeBId;
    h1b.face = oldH1.face;

    HalfEdge h1a;
    h1a.id = h1aId;
    h1a.origin = mId;
    h1a.twin = h0aId;
    h1a.edge = edgeAId;
    h1a.face = oldH1.face;

    // h0 side: A -> M -> B replaces A -> B. A wire half-edge stays wire; a face-loop one gets
    // h0a/h0b spliced in exactly where h0 sat.
    if (oldH0.face != kInvalidId) {
        h0a.prev = oldH0.prev;
        h0a.next = h0bId;
        h0b.prev = h0aId;
        h0b.next = oldH0.next;
        halfEdges_.at(oldH0.prev).next = h0aId;
        halfEdges_.at(oldH0.next).prev = h0bId;
        if (faces_.at(oldH0.face).halfEdge == h0) {
            faces_.at(oldH0.face).halfEdge = h0aId;
        }
    } else {
        h0a.prev = kInvalidId;
        h0a.next = kInvalidId;
        h0b.prev = kInvalidId;
        h0b.next = kInvalidId;
    }

    // h1 side: B -> M -> A, mirrored versus h0 -- h1b (B -> M) comes before h1a (M -> A).
    if (oldH1.face != kInvalidId) {
        h1b.prev = oldH1.prev;
        h1b.next = h1aId;
        h1a.prev = h1bId;
        h1a.next = oldH1.next;
        halfEdges_.at(oldH1.prev).next = h1bId;
        halfEdges_.at(oldH1.next).prev = h1aId;
        if (faces_.at(oldH1.face).halfEdge == h1) {
            faces_.at(oldH1.face).halfEdge = h1bId;
        }
    } else {
        h1b.prev = kInvalidId;
        h1b.next = kInvalidId;
        h1a.prev = kInvalidId;
        h1a.next = kInvalidId;
    }

    halfEdges_.emplace(h0aId, h0a);
    halfEdges_.emplace(h0bId, h0b);
    halfEdges_.emplace(h1bId, h1b);
    halfEdges_.emplace(h1aId, h1a);

    // Emission order matters: edgeA/edgeB must be reported CREATED before either affected face's
    // faceLoopModified. Forward replay needs the edges to exist first; a backward undo needs both
    // faces undone before edgeA/edgeB's own undo releases the original edge.
    insertEdgeRecord(edgeAId, h0aId, h1aId, vA, mId);
    insertEdgeRecord(edgeBId, h0bId, h1bId, mId, vB);

    // The new half-edges are chained in now, so faceVertexLoop already yields the post-split loop.
    if (oldH0.face != kInvalidId) {
        notifyFaceLoopModified(oldH0.face, loopBefore0, faceVertexLoop(oldH0.face));
    }
    if (oldH1.face != kInvalidId) {
        notifyFaceLoopModified(oldH1.face, loopBefore1, faceVertexLoop(oldH1.face));
    }

    // Outgoing updates: A swaps h0 -> h0a, B swaps h1 -> h1b; M is new and gains both of its own.
    auto& outA = vertices_.at(vA).outgoing;
    std::replace(outA.begin(), outA.end(), h0, h0aId);
    auto& outB = vertices_.at(vB).outgoing;
    std::replace(outB.begin(), outB.end(), h1, h1bId);
    vertices_.at(mId).outgoing.push_back(h0bId);
    vertices_.at(mId).outgoing.push_back(h1aId);

    // The original edge record is fully erased and replaced by two new ones, never reused with a
    // changed endpoint -- hence no edgeEndpointsModified event on ModelJournal.
    eraseEdgeRecord(edgeId, h0, h1);

    SplitEdgeResult result;
    result.newVertex = mId;
    result.edgeA = edgeAId;
    result.edgeB = edgeBId;
    result.ok = true;
    return result;
}

SplitFaceResult Model::splitFaceByChord(Id faceId, Id chordEdgeId) {
    const auto faceIt = faces_.find(faceId);
    const auto edgeIt = edges_.find(chordEdgeId);
    if (faceIt == faces_.end() || edgeIt == edges_.end()) {
        return {};  // ok=false, no mutation -- unknown id
    }

    const HalfEdge& ch0 = halfEdges_.at(edgeIt->second.halfEdges[0]);
    const HalfEdge& ch1 = halfEdges_.at(edgeIt->second.halfEdges[1]);
    if (ch0.face != kInvalidId || ch1.face != kInvalidId) {
        // Not a clean interior chord -- another face already claimed one side.
        return {};
    }
    const Id vA = ch0.origin;
    const Id vB = ch1.origin;

    const std::vector<Id> loop = faceVertexLoop(faceId);
    const std::size_t n = loop.size();
    const auto itA = std::find(loop.begin(), loop.end(), vA);
    const auto itB = std::find(loop.begin(), loop.end(), vB);
    if (itA == loop.end() || itB == loop.end() || itA == itB) {
        return {};  // chord's endpoints aren't both distinct loop vertices
    }
    const std::size_t iA = static_cast<std::size_t>(itA - loop.begin());
    const std::size_t iB = static_cast<std::size_t>(itB - loop.begin());

    // Walk loop indices forward (wrapping) from `from` to `to`, both ends included: the path starts
    // and ends at vA/vB, so createFaceOnLoop's wrap-around pairing finds chordEdgeId for free.
    const auto pathForward = [&](std::size_t from, std::size_t to) {
        std::vector<Id> path;
        std::size_t i = from;
        while (true) {
            path.push_back(loop[i]);
            if (i == to) {
                break;
            }
            i = (i + 1) % n;
        }
        return path;
    };

    const std::vector<Id> pathAB = pathForward(iA, iB);
    const std::vector<Id> pathBA = pathForward(iB, iA);
    // A 2-vertex path means vA/vB are adjacent: chordEdgeId would duplicate a boundary edge.
    if (pathAB.size() < 3 || pathBA.size() < 3) {
        return {};
    }

    // --- Guards passed: every step below commits to mutating the model. ---

    const Vec3 normal = faceIt->second.normal;
    dissolveFace(faceId);

    SplitFaceResult result;
    result.faceA = createFaceOnLoop(pathAB, normal);
    result.faceB = createFaceOnLoop(pathBA, normal);
    result.ok = (result.faceA != kInvalidId) && (result.faceB != kInvalidId);
    return result;
}

const std::unordered_map<Id, Vertex>& Model::vertices() const {
    return vertices_;
}

const std::unordered_map<Id, Edge>& Model::edges() const {
    return edges_;
}

const std::unordered_map<Id, HalfEdge>& Model::halfEdges() const {
    return halfEdges_;
}

const std::unordered_map<Id, Face>& Model::faces() const {
    return faces_;
}

const Vertex* Model::findVertex(Vec3 pos, double tol) const {
    const Vertex* best = nullptr;
    double bestDist = 0.0;
    for (const auto& [id, v] : vertices_) {
        const double d = distance(v.pos, pos);
        if (d <= tol && (best == nullptr || d < bestDist)) {
            best = &v;
            bestDist = d;
        }
    }
    return best;
}

const Vertex* Model::vertex(Id id) const {
    const auto it = vertices_.find(id);
    return it != vertices_.end() ? &it->second : nullptr;
}

const Edge* Model::edge(Id id) const {
    const auto it = edges_.find(id);
    return it != edges_.end() ? &it->second : nullptr;
}

const HalfEdge* Model::halfEdge(Id id) const {
    const auto it = halfEdges_.find(id);
    return it != halfEdges_.end() ? &it->second : nullptr;
}

const Face* Model::face(Id id) const {
    const auto it = faces_.find(id);
    return it != faces_.end() ? &it->second : nullptr;
}

std::vector<Id> Model::faceVertexLoop(Id faceId) const {
    const auto faceIt = faces_.find(faceId);
    if (faceIt == faces_.end()) {
        return {};
    }

    std::vector<Id> loop;
    const Id start = faceIt->second.halfEdge;
    Id cur = start;
    while (cur != kInvalidId) {
        const auto heIt = halfEdges_.find(cur);
        if (heIt == halfEdges_.end()) {
            return {};  // defensive: corrupt cycle
        }
        loop.push_back(heIt->second.origin);
        cur = heIt->second.next;
        if (cur == start) {
            break;
        }
    }
    return loop;
}

ExtrudeResult Model::extrudeFace(Id faceId, double distance) {
    const auto faceIt = faces_.find(faceId);
    if (faceIt == faces_.end() || std::fabs(distance) < kMergeTol) {
        return {};  // ok=false, no mutation
    }

    const std::vector<Id> loop = faceVertexLoop(faceId);
    const std::size_t n = loop.size();
    if (n < 3) {
        return {};  // defensive: corrupt/degenerate face
    }
    const Vec3 normal = faceIt->second.normal;

    // Snapshot base positions before anything is dissolved/created.
    std::vector<Vec3> basePositions(n);
    for (std::size_t i = 0; i < n; ++i) {
        basePositions[i] = vertices_.at(loop[i]).pos;
    }
    std::vector<Vec3> topPositions(n);
    for (std::size_t i = 0; i < n; ++i) {
        topPositions[i] = basePositions[i] + normal * distance;
    }

    // "Away from the extrusion" flips with the sign of distance; the literal -normal/+normal choice
    // below is only outward-correct for positive distance, so dirSign corrects the inward case.
    const double dirSign = (distance > 0.0) ? 1.0 : -1.0;

    // n new "top" vertices (raw records -- allocId, no merge).
    std::vector<Id> newVerts(n);
    for (std::size_t i = 0; i < n; ++i) {
        const Id id = allocId();
        insertVertexRecord(id, topPositions[i]);
        newVerts[i] = id;
    }

    // Vertical edges loop[i]--newVerts[i] and cap edges newVerts[i]--newVerts[i+1], raw record path.
    for (std::size_t i = 0; i < n; ++i) {
        createEdgeRecord(loop[i], newVerts[i]);
    }
    for (std::size_t i = 0; i < n; ++i) {
        createEdgeRecord(newVerts[i], newVerts[(i + 1) % n]);
    }

    dissolveFace(faceId);

    // Side quads {loop[i], loop[i+1], newVerts[i+1], newVerts[i]}, Newell normal over the 4 actual
    // positions. Created BEFORE the cap and the recreated original: on an already-shared boundary
    // edge only one winding survives dissolveFace and the side face must be the one to claim it.
    // dirSign flips each quad's loop ORDER as well as its normal, or winding and normal disagree.
    std::vector<Id> sideFaces(n);
    for (std::size_t i = 0; i < n; ++i) {
        const std::size_t j = (i + 1) % n;
        const std::vector<Id> quadLoop =
            dirSign > 0.0 ? std::vector<Id>{loop[i], loop[j], newVerts[j], newVerts[i]}
                          : std::vector<Id>{newVerts[i], newVerts[j], loop[j], loop[i]};
        const std::vector<Vec3> quadPositions = {basePositions[i], basePositions[j], topPositions[j],
                                                  topPositions[i]};
        const Vec3 sideNormal = newellNormal(quadPositions) * dirSign;
        sideFaces[i] = createFaceOnLoop(quadLoop, sideNormal);
    }

    // Cap face on the new top loop, facing toward the extrusion direction.
    std::vector<Id> capLoop = newVerts;
    if (dirSign < 0.0) {
        std::reverse(capLoop.begin(), capLoop.end());
    }
    const Id capFace = createFaceOnLoop(capLoop, normal * dirSign);

    // Recreate the original face reversed, its normal now pointing away from the extrusion. On an
    // already-shared boundary edge both windings may be taken; kInvalidId with no mutation is the
    // correct outcome. For dirSign < 0 the ORIGINAL order is already the away-facing winding.
    const std::vector<Id> bottomLoop = dirSign > 0.0 ? std::vector<Id>(loop.rbegin(), loop.rend()) : loop;
    createFaceOnLoop(bottomLoop, normal * -dirSign);  // not surfaced in ExtrudeResult

    ExtrudeResult result;
    result.capFace = capFace;
    result.sideFaces = std::move(sideFaces);
    result.newVertices = std::move(newVerts);
    result.ok = true;
    return result;
}

Id Model::addFaceOnLoop(const std::vector<Id>& vertexLoop, Vec3 normal) {
    return createFaceOnLoop(vertexLoop, normal);
}

bool Model::isRestorableId(Id id) const {
    if (id == kInvalidId || id >= nextId_) {
        return false;
    }
    return vertices_.find(id) == vertices_.end() && edges_.find(id) == edges_.end() &&
           halfEdges_.find(id) == halfEdges_.end() && faces_.find(id) == faces_.end();
}

bool Model::restoreNextId(Id nextId) {
    if (nextId < nextId_) {
        return false;  // restoring backward could collide with ids already in use
    }
    nextId_ = nextId;
    return true;
}

bool Model::restoreVertex(Id id, Vec3 pos) {
    if (!isRestorableId(id)) {
        return false;
    }
    insertVertexRecord(id, pos);
    return true;
}

bool Model::restoreEdge(Id id, Id v0, Id v1) {
    if (!isRestorableId(id) || v0 == v1) {
        return false;
    }
    if (vertices_.find(v0) == vertices_.end() || vertices_.find(v1) == vertices_.end()) {
        return false;  // unknown endpoint -- corrupt file
    }
    createEdgeRecordWithId(id, v0, v1);
    return true;
}

bool Model::restoreFace(Id id, const std::vector<Id>& vertexLoop) {
    if (!isRestorableId(id) || vertexLoop.size() < 3) {
        return false;
    }

    // Positions in stored order for the Newell normal; doubles as an early unknown-vertex guard.
    std::vector<Vec3> positions;
    positions.reserve(vertexLoop.size());
    for (Id v : vertexLoop) {
        const auto it = vertices_.find(v);
        if (it == vertices_.end()) {
            return false;  // unknown vertex -- corrupt file, no mutation
        }
        positions.push_back(it->second.pos);
    }
    const Vec3 normal = newellNormal(positions);

    std::vector<Id> chosen;
    Vec3 faceNormal;
    // allowTwinFallback = false: the saved winding is authoritative.
    if (!resolveLoopWinding(vertexLoop, normal, /*allowTwinFallback=*/false, chosen, faceNormal)) {
        return false;  // missing or already-claimed half-edge in the stated winding -- no mutation
    }
    commitFace(id, chosen, faceNormal);
    return true;
}

// -- Rollback primitives -- contract in model.h.

bool Model::eraseVertexForRollback(Id id) {
    const auto it = vertices_.find(id);
    if (it == vertices_.end() || !it->second.outgoing.empty()) {
        return false;
    }
    eraseVertexRecord(id);
    return true;
}

bool Model::eraseEdgeForRollback(Id id) {
    const auto edgeIt = edges_.find(id);
    if (edgeIt == edges_.end()) {
        return false;
    }
    const Id he0 = edgeIt->second.halfEdges[0];
    const Id he1 = edgeIt->second.halfEdges[1];
    if (halfEdges_.at(he0).face != kInvalidId || halfEdges_.at(he1).face != kInvalidId) {
        return false;  // not wire on both sides -- a face still claims it, roll that back first
    }

    // Remove from origins' outgoing lists; no orphan GC here -- the transaction delta's own
    // vertexDeleted entries drive vertex removal.
    const Id v0 = halfEdges_.at(he0).origin;
    const Id v1 = halfEdges_.at(he1).origin;
    auto& out0 = vertices_.at(v0).outgoing;
    out0.erase(std::remove(out0.begin(), out0.end(), he0), out0.end());
    auto& out1 = vertices_.at(v1).outgoing;
    out1.erase(std::remove(out1.begin(), out1.end(), he1), out1.end());

    eraseEdgeRecord(id, he0, he1);
    return true;
}

bool Model::eraseFaceForRollback(Id id) {
    if (faces_.find(id) == faces_.end()) {
        return false;
    }
    dissolveFace(id);
    return true;
}

bool Model::setVertexPosForRollback(Id id, Vec3 pos) {
    if (vertices_.find(id) == vertices_.end()) {
        return false;
    }
    setVertexPosRecord(id, pos);
    recomputeIncidentFaceNormals({id});
    return true;
}

bool Model::translateVertices(const std::vector<Id>& vertexIds, Vec3 delta) {
    if (length(delta) < kEps) {
        return false;
    }
    for (Id id : vertexIds) {
        if (vertices_.find(id) == vertices_.end()) {
            return false;
        }
    }

    const std::unordered_set<Id> movedSet(vertexIds.begin(), vertexIds.end());
    for (Id id : movedSet) {
        setVertexPosRecord(id, vertices_.at(id).pos + delta);
    }

    recomputeIncidentFaceNormals(movedSet);
    return true;
}

bool Model::transformVertices(const std::vector<Id>& vertexIds, const Transform& xf) {
    for (Id id : vertexIds) {
        if (vertices_.find(id) == vertices_.end()) {
            return false;
        }
    }

    // Identity guard: require at least one listed vertex to move by kEps or more under xf.
    const bool anyMoved = std::any_of(vertexIds.begin(), vertexIds.end(), [this, &xf](Id id) {
        const Vec3& pos = vertices_.at(id).pos;
        return distance(xf.apply(pos), pos) >= kEps;
    });
    if (!anyMoved) {
        return false;
    }

    const std::unordered_set<Id> movedSet(vertexIds.begin(), vertexIds.end());
    for (Id id : movedSet) {
        setVertexPosRecord(id, xf.apply(vertices_.at(id).pos));
    }

    recomputeIncidentFaceNormals(movedSet);
    return true;
}

void Model::recomputeIncidentFaceNormals(const std::unordered_set<Id>& movedIds) {
    // Recompute the Newell normal of every face whose loop contains a moved vertex; full face scan.
    for (auto& [faceId, face] : faces_) {
        const std::vector<Id> loop = faceVertexLoop(faceId);
        const bool touched = std::any_of(loop.begin(), loop.end(),
                                          [&movedIds](Id v) { return movedIds.count(v) != 0; });
        if (!touched) {
            continue;
        }
        std::vector<Vec3> positions;
        positions.reserve(loop.size());
        for (Id v : loop) {
            positions.push_back(vertices_.at(v).pos);
        }
        face.normal = newellNormal(positions);
    }
}

}  // namespace plnr::geo
