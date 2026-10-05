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

// Downward closure of seeds: a Face adds its boundary edges, an Edge its endpoints. Unknown ids
// are ignored.
EntitySet closureOf(const Model& model, const std::vector<std::pair<EntityKind, Id>>& seeds);

// Copies set into dst through xf, restating each face with its transformed normal (no auto-detect).
// Ids do not survive; a face reappears only if all its edges transferred; isolated vertices drop.
void copyInto(Model& dst, const Model& src, const EntitySet& set, const Transform& xf,
               std::vector<Id>* createdEdges = nullptr);

// Removes set's edges, dissolving every face that uses one (even outside set) and GC'ing orphaned
// vertices.
void removeFrom(Model& m, const EntitySet& set);

}  // namespace plnr::geo
