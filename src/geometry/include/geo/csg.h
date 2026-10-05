#pragma once

#include <vector>

#include <geo/model.h>
#include <geo/scene.h>
#include <geo/vec3.h>

namespace plnr::geo::csg {

// Boolean CSG between two solids, producing a polygon soup (MeshSpec). Never mutates a Model or
// Scene; failures return Result::ok = false.

enum class Op {
    Union,      // A OR B
    Subtract,   // a MINUS b; kept cutter fragments come out FLIPPED (they bound the cavity).
    Intersect,  // A AND B
};

// toWorld may mirror.
struct Operand {
    const Model* model{};
    Transform toWorld;  // operand geometry -> common world frame
};

// Output polygon in world coordinates; may be an unmerged convex fragment with T-vertices
// (mergeCoplanarFaces re-merges).
struct PolyFace {
    std::vector<int> loop;   // indices into MeshSpec::vertices, wound so cross(v1-v0, v2-v0) is `normal`
    Vec3 normal;             // unit, outward w.r.t. the RESULT solid
    int operandIndex{};      // provenance: 0 = a, 1 = b
    Id sourceFaceId{};       // provenance: Face id in that operand's Model
};

struct MeshSpec {
    std::vector<Vec3> vertices;  // deduplicated within kMergeTol
    std::vector<PolyFace> faces;
};

// ok = false only on structural failure (null model, no usable triangle, BSP guard breach).
// An empty result is a success (ok = true, 0 faces).
struct Result {
    bool ok{};
    MeshSpec mesh;
};

// BSP-clipping boolean in the shared world frame; deterministic (source faces visited in id order).
// Fails past kMaxBspDepth tree levels or kMaxPolygons fragments.
Result apply(const Operand& a, const Operand& b, Op op);

// Signed volume: positive for an outward-oriented closed shell. Malformed loops are skipped.
double meshVolume(const MeshSpec& mesh);

// Opt-in post-processing on apply()'s raw fragment soup.

// Heals T-vertices, then merges coplanar same-provenance fragments into maximal simple polygons.
// Keeps all boundary vertices: dropping collinear ones reopens a T-crack on a neighbouring face.
// A degenerate group falls back to its healed fragments.
MeshSpec mergeCoplanarFaces(const MeshSpec& mesh);

// Every undirected edge used by exactly two faces in opposite directions, judged verbatim (no
// welding): a raw apply() soup with T-vertices fails. No faces = watertight.
bool meshIsWatertight(const MeshSpec& mesh);

// Union with every enclosed void removed (result is merged). A shell is dropped only when a ray
// cast proves it contained in another; jitter is a fixed table (never random), and an unresolved
// cast keeps the shell.
Result outerShell(const Operand& a, const Operand& b);

// The three pieces of an operand pair, each merged. Empty pieces are fine; ok = false only when
// an underlying apply() fails.
struct SplitResult {
    bool ok{};
    MeshSpec aMinusB;
    MeshSpec bMinusA;
    MeshSpec aIntersectB;
};

SplitResult split(const Operand& a, const Operand& b);

}  // namespace plnr::geo::csg
