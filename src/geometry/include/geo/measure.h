#pragma once

#include <geo/model.h>

namespace plnr::geo {

// Straight-line distance between edgeId's two endpoint vertices. 0.0 if
// edgeId (or either endpoint half-edge/vertex it resolves to) is unknown.
double edgeLength(const Model& model, Id edgeId);

// Sum of triangle areas from triangulate(model, faceId) -- reuses the
// existing ear-clipping triangulation rather than re-deriving face area
// independently, so the two stay consistent by construction. 0.0 if faceId
// is unknown or degenerate (triangulate() returns an empty list).
double faceArea(const Model& model, Id faceId);

}  // namespace plnr::geo
