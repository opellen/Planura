#pragma once

#include <geo/model.h>

namespace plnr::geo {

// 0.0 if the edge is unknown.
double edgeLength(const Model& model, Id edgeId);

// Sum of triangulate()'s triangle areas, so it matches the triangulation. 0.0 if the face is
// unknown or degenerate.
double faceArea(const Model& model, Id faceId);

}  // namespace plnr::geo
