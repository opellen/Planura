#pragma once

#include <geo/model.h>
#include <geo/scene.h>

namespace plnr::geo {

// Watertight-solid classification for a Model: three independent checks over half-edge invariants.
struct SolidInfo {
    bool solid{};              // all checks passed
    bool wireEdges{};          // (a) failed: some half-edge carries no face (stray edge or open border/hole)
    bool nonManifoldVertex{};  // (b) failed: a vertex's face fan is not a single umbrella cycle (bowtie)
    bool nonPositiveVolume{};  // (c) failed: total signed volume <= eps (globally reversed shell or degenerate)
};

// Classifies model as a closed, manifold, positively-oriented solid:
//  (a) every half-edge belongs to a face (an empty model instead fails (c));
//  (b) each vertex's fan is one umbrella cycle, walked h -> twin(prev(h)); runs only if (a) passed;
//  (c) solidVolume(model) > kEps, always evaluated.
SolidInfo isSolid(const Model& model);

// Signed volume via the divergence theorem: sum of dot(v0, cross(v1, v2)) / 6 over every face's
// triangles. Positive for an outward-oriented closed shell and the true enclosed volume once
// isSolid(model).solid; otherwise unguarded -- an open model still sums whatever its faces give.
double solidVolume(const Model& model);

// def is a solid iff it owns no nested Instance and its own model passes isSolid.
bool isSolidDefinition(const Definition& def);

}  // namespace plnr::geo
