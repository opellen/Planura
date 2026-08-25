#pragma once

#include <vector>

#include <geo/model.h>

namespace plnr::geo {

// Instance: a whole group/component instance, selected/hidden/moved as a unit
// rather than through its constituent geometry. Resolves via geo::Scene
// (Scene::findInstance), never geo::Model -- see collectVertices's Instance case below.
enum class EntityKind { Vertex, Edge, Face, Instance };

// Returns the vertex ids that make up the given entity; empty if id is unknown for that kind.
//  - Vertex -> {id}; Edge -> its two endpoint ids; Face -> its ordered loop (Model::faceVertexLoop).
//  - Instance -> always {} (no vertices of its own; it names a child Definition in the owning Scene) --
//    GeometryApi::moveEntity moves an Instance via Scene::setInstanceTransform instead.
inline std::vector<Id> collectVertices(const Model& model, EntityKind kind, Id id) {
    switch (kind) {
        case EntityKind::Vertex: {
            if (model.vertex(id) != nullptr) {
                return {id};
            }
            return {};
        }
        case EntityKind::Edge: {
            const Edge* e = model.edge(id);
            if (e == nullptr) {
                return {};
            }
            const HalfEdge* h0 = model.halfEdge(e->halfEdges[0]);
            const HalfEdge* h1 = model.halfEdge(e->halfEdges[1]);
            if (h0 == nullptr || h1 == nullptr) {
                return {};
            }
            return {h0->origin, h1->origin};
        }
        case EntityKind::Face:
            return model.faceVertexLoop(id);
        case EntityKind::Instance:
            return {};
    }
    return {};
}

}  // namespace plnr::geo
