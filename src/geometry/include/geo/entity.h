#pragma once

#include <vector>

#include <geo/model.h>

namespace plnr::geo {

// Instance = a whole group/component instance; resolves via Scene::findInstance, never Model.
enum class EntityKind { Vertex, Edge, Face, Instance };

// Vertex ids of an entity (a Face gives its ordered loop); empty for an unknown id.
// An Instance always gives {}: move it via Scene::setInstanceTransform instead.
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
