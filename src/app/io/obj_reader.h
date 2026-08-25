#pragma once

#include <cstddef>
#include <vector>

#include <QByteArray>
#include <QString>

#include <geo/vec3.h>

// OBJ importer: readObj() below parses OBJ text into a deterministic
// vertex/face mesh. Builds no geo::Model/Scene itself (Qt-free domain
// construction is GeometryApi::importMesh's job)
namespace plnr::io {

// Axis-converted to internal (Z-up, inches) space. vertices[i] is OBJ index
// i+1 verbatim (no dedup). faces[] entries are >=3 0-based indices in file
// order, never triangulated (kernel supports n-gons)
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

// Parses bytes as Wavefront OBJ text. "v x y z" -> Vec3; "f" (>=3 corners,
// v/vt/vn variants, only v read) kept as one polygon, never triangulated.
// Other record types are tolerated and ignored. ok==false on an empty file,
// a malformed v/f record, or an out-of-range face index.
ReadObjResult readObj(const QByteArray& bytes);

}  // namespace plnr::io
