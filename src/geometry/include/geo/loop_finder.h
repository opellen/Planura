#pragma once

#include <optional>
#include <vector>

#include <geo/model.h>

namespace plnr::geo {

// A closed loop of vertices/edges discovered around a freshly inserted edge,
// plus its unit Newell normal. vertexLoop is ordered, implicitly closed (last
// connects back to first, not repeated); edgeLoop[i] connects vertexLoop[i]
// to vertexLoop[(i + 1) % n].
struct LoopCandidate {
    std::vector<Id> vertexLoop;
    std::vector<Id> edgeLoop;
    Vec3 normal;
};

// Searches for the shortest closed loop through newEdgeId (already inserted
// by addEdge) via BFS over the vertex graph excluding newEdgeId, from its
// second endpoint back to its first; ties break by ascending edge-id. nullopt
// if no path closes, <3 vertices, degenerate/non-planar, self-intersecting
// in-plane, or an existing face already uses this edge set.
std::optional<LoopCandidate> findLoopForNewEdge(const Model& model, Id newEdgeId);

}  // namespace plnr::geo
