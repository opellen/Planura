#pragma once

#include <vector>

#include <geo/model.h>

namespace plnr::geo {

// Ear-clipping triangulation of a face's boundary loop, projected to its
// plane. Returns vertex ids, 3 per triangle, wound consistently with
// Face.normal (cross(b - a, c - a) points the same way as the face normal
// for each returned triangle (a, b, c)). Returns an empty vector if faceId
// is unknown or the face is degenerate (fewer than 3 distinct boundary
// vertices, zero-length normal, or a zero-area projection).
std::vector<Id> triangulate(const Model& model, Id faceId);

}  // namespace plnr::geo
