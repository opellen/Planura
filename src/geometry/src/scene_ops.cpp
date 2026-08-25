#include <geo/scene_ops.h>

#include <algorithm>
#include <unordered_set>

#include <geo/select.h>

namespace plnr::geo {

namespace {

// Whether m already has a face over exactly this vertex set (winding ignored). Guards the
// &dst == &src self-copy: re-stating a face that already exists would claim the twin winding and
// stack a duplicate with an inverted normal.
bool faceExistsOnLoop(const Model& m, std::vector<Id> loop) {
    std::sort(loop.begin(), loop.end());
    for (const auto& [id, face] : m.faces()) {
        (void)face;
        std::vector<Id> other = m.faceVertexLoop(id);
        if (other.size() != loop.size()) continue;
        std::sort(other.begin(), other.end());
        if (other == loop) return true;
    }
    return false;
}

}  // namespace

EntitySet closureOf(const Model& model, const std::vector<std::pair<EntityKind, Id>>& seeds) {
    EntitySet result;
    std::unordered_set<Id> seenVertices, seenEdges, seenFaces;

    // Worklist cascade: a Face pushes its boundary edges, an Edge its endpoint vertices, a Vertex
    // nothing further. Each kind keeps its own seen-set.
    std::vector<std::pair<EntityKind, Id>> work(seeds.begin(), seeds.end());

    while (!work.empty()) {
        const auto [kind, id] = work.back();
        work.pop_back();

        switch (kind) {
            case EntityKind::Vertex: {
                if (model.vertex(id) == nullptr) continue;  // unknown id -- ignored
                if (seenVertices.insert(id).second) {
                    result.vertices.push_back(id);
                }
                break;
            }
            case EntityKind::Edge: {
                if (model.edge(id) == nullptr) continue;
                if (seenEdges.insert(id).second) {
                    result.edges.push_back(id);
                    for (Id vId : collectVertices(model, EntityKind::Edge, id)) {
                        work.emplace_back(EntityKind::Vertex, vId);
                    }
                }
                break;
            }
            case EntityKind::Face: {
                if (model.face(id) == nullptr) continue;
                if (seenFaces.insert(id).second) {
                    result.faces.push_back(id);
                    for (Id eId : faceBoundaryEdges(model, id)) {
                        work.emplace_back(EntityKind::Edge, eId);
                    }
                }
                break;
            }
            case EntityKind::Instance:
                // An Instance isn't part of this Model; treated as any other unknown id.
                continue;
        }
    }

    std::sort(result.vertices.begin(), result.vertices.end());
    std::sort(result.edges.begin(), result.edges.end());
    std::sort(result.faces.begin(), result.faces.end());
    return result;
}

void copyInto(Model& dst, const Model& src, const EntitySet& set, const Transform& xf,
               std::vector<Id>* createdEdges) {
    // Replay in ascending id order so dst's id assignment is deterministic regardless of caller order.
    std::vector<Id> edgeIds = set.edges;
    std::sort(edgeIds.begin(), edgeIds.end());

    for (Id edgeId : edgeIds) {
        const Edge* e = src.edge(edgeId);
        if (e == nullptr) continue;  // defensive: stale id in a caller-built set
        const HalfEdge* h0 = src.halfEdge(e->halfEdges[0]);
        const HalfEdge* h1 = src.halfEdge(e->halfEdges[1]);
        if (h0 == nullptr || h1 == nullptr) continue;
        const Vertex* v0 = src.vertex(h0->origin);
        const Vertex* v1 = src.vertex(h1->origin);
        if (v0 == nullptr || v1 == nullptr) continue;
        // detectFaces=false: auto-detection knows nothing of the source winding and could close a
        // face on the right vertex loop with the WRONG orientation. Every face is instead restated
        // below from set.faces with the exact winding src recorded.
        const AddEdgeResult r = dst.addEdge(xf.apply(v0->pos), xf.apply(v1->pos), /*detectFaces=*/false);
        if (r.created && createdEdges != nullptr) {
            createdEdges->push_back(r.edge);
        }
    }

    // Restate each face in `set`: map its src vertex loop to dst by transformed position and create
    // it via addFaceOnLoop with the source face's transformed normal. This loop is the only place
    // copyInto produces faces -- exactly the ones named in `set`.
    std::vector<Id> faceIds = set.faces;
    std::sort(faceIds.begin(), faceIds.end());
    for (Id faceId : faceIds) {
        const Face* face = src.face(faceId);
        if (face == nullptr) continue;  // defensive: stale id in a caller-built set

        std::vector<Id> dstLoop;
        bool resolved = true;
        for (Id vId : src.faceVertexLoop(faceId)) {
            const Vertex* v = src.vertex(vId);
            const Vertex* dv = (v != nullptr) ? dst.findVertex(xf.apply(v->pos)) : nullptr;
            if (dv == nullptr) {
                resolved = false;  // a boundary edge of this face wasn't in the set
                break;
            }
            dstLoop.push_back(dv->id);
        }
        // Purely the &dst == &src idempotence guard; detectFaces=false leaves no race to guard.
        if (!resolved || faceExistsOnLoop(dst, dstLoop)) continue;

        dst.addFaceOnLoop(dstLoop, normalized(xf.applyVector(face->normal)));
    }
}

void removeFrom(Model& m, const EntitySet& set) {
    for (Id edgeId : set.edges) {
        m.removeEdge(edgeId);
    }
}

}  // namespace plnr::geo
