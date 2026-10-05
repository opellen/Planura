#pragma once

#include <geo/model.h>
#include <geo/scene.h>

namespace plnr::geo {

// Watertight-solid classification; each flag marks a failed check.
struct SolidInfo {
    bool solid{};              // all checks passed
    bool wireEdges{};          // (a) failed: some half-edge carries no face (stray edge or open border/hole)
    bool nonManifoldVertex{};  // (b) failed: a vertex's face fan is not a single umbrella cycle (bowtie)
    bool nonPositiveVolume{};  // (c) failed: total signed volume <= eps (globally reversed shell or degenerate)
};

// (b) runs only if (a) passed; (c) always runs, so an empty model fails (c), not (a).
SolidInfo isSolid(const Model& model);

// Signed volume; exact once isSolid passes. Unguarded otherwise: an open model still sums its faces.
double solidVolume(const Model& model);

// def is a solid iff it owns no nested Instance and its own model passes isSolid.
bool isSolidDefinition(const Definition& def);

}  // namespace plnr::geo
