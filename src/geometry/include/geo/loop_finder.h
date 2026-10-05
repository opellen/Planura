#pragma once

#include <optional>
#include <vector>

#include <geo/model.h>

namespace plnr::geo {

// Closed loop found around a new edge, with its unit Newell normal. vertexLoop is implicitly
// closed (first not repeated); edgeLoop[i] joins vertexLoop[i] to vertexLoop[(i + 1) % n].
struct LoopCandidate {
    std::vector<Id> vertexLoop;
    std::vector<Id> edgeLoop;
    Vec3 normal;
};

// Shortest closed loop through the already-inserted newEdgeId (BFS without that edge; ties by
// ascending edge id). nullopt if none closes, or the loop is degenerate, non-planar,
// self-intersecting, or already a face.
std::optional<LoopCandidate> findLoopForNewEdge(const Model& model, Id newEdgeId);

}  // namespace plnr::geo
