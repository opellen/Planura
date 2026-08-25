#pragma once

#include <QByteArray>

#include "agent/geometry_api.h"
#include "agent/tag_store.h"

// OBJ exporter: walks GeometryApi/TagStore into Wavefront OBJ text.
namespace plnr::io {

// Serializes the live VISIBLE scene graph to Wavefront OBJ text (v/vn only,
// triangulated, world-space baked, one `g <path>` group per instance). Axis
// conversion is the identity and units are inches, unscaled (matches
// the reference modeler's own OBJ-export default).
QByteArray writeObj(const agent::GeometryApi& geometry, const agent::TagStore& tags);

}  // namespace plnr::io
