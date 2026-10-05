#pragma once

#include <cstddef>
#include <vector>

#include <QByteArray>
#include <QString>

#include <geo/vec3.h>

// OBJ importer: parses OBJ text into a deterministic vertex/face mesh; builds
// no geo::Model itself (GeometryApi::importMesh does).
namespace plnr::io {

// Axis-converted to internal (Z-up, inches). vertices[i] is OBJ index i+1,
// no dedup. faces[]: >=3 0-based indices in file order, never triangulated.
struct ObjMesh {
  std::vector<geo::Vec3> vertices;
  std::vector<std::vector<std::size_t>> faces;
};

// ok==true: mesh holds the parse result (syntax only, not importability).
// ok==false: error names the problem; errorLine is the 1-based line (0 for
// whole-file errors), already embedded in error's text.
struct ReadObjResult {
  bool ok{};
  ObjMesh mesh;
  QString error;
  int errorLine{};
};

// Parses Wavefront OBJ text; only v/f records are read, others ignored.
// ok==false on an empty file, a malformed v/f record, or an out-of-range index.
ReadObjResult readObj(const QByteArray &bytes);

} // namespace plnr::io
