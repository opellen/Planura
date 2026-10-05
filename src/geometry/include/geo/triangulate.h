#pragma once

#include <vector>

#include <geo/model.h>

namespace plnr::geo {

// Ear-clipping in the face plane: 3 vertex ids per triangle, wound so cross(b - a, c - a) follows
// Face.normal. Empty if the face is unknown or degenerate.
std::vector<Id> triangulate(const Model& model, Id faceId);

}  // namespace plnr::geo
