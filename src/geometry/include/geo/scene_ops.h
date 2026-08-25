#pragma once

// Model surgery for Make Group / Make Component / Explode: moving geometry between Models.

#include <utility>
#include <vector>

#include <geo/entity.h>
#include <geo/model.h>
#include <geo/scene.h>

namespace plnr::geo {

// One selection's worth of entities by kind; each vector is sorted ascending by id.
struct EntitySet {
    std::vector<Id> vertices, edges, faces;
};

// Selection closure over seeds, walking DOWNWARD to constituents only: a Face pulls in its boundary
// edges, an Edge its two endpoint vertices, a Vertex just itself. Dedups across overlapping seeds;
// an unknown seed id is silently ignored.
EntitySet closureOf(const Model& model, const std::vector<std::pair<EntityKind, Id>>& seeds);

// Re-adds every edge of set into dst with detectFaces=false, then restates each face via
// addFaceOnLoop with the source face's transformed normal, bypassing auto-detection. ids do NOT
// survive the move; a face reappears only if all its boundary edges transferred, and isolated
// vertices are dropped. createdEdges, if non-null, collects this call's created edge ids.
void copyInto(Model& dst, const Model& src, const EntitySet& set, const Transform& xf,
               std::vector<Id>* createdEdges = nullptr);

// Removes every edge in set via Model::removeEdge, dissolving any face that references it and GC'ing
// orphaned endpoint vertices. A face outside `set` that shares a removed edge dissolves too.
void removeFrom(Model& m, const EntitySet& set);

}  // namespace plnr::geo
