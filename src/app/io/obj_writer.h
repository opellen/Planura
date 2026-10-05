#pragma once

#include <QByteArray>

#include "agent/geometry_api.h"
#include "agent/tag_store.h"

// OBJ exporter: walks GeometryApi/TagStore into Wavefront OBJ text.
namespace plnr::io {

// Visible scene graph -> OBJ text (v/vn, triangulated, world-space, one
// `g <path>` per instance). No axis conversion; units are inches, unscaled.
QByteArray writeObj(const agent::GeometryApi &geometry,
                    const agent::TagStore &tags);

} // namespace plnr::io
