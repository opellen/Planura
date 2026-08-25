#pragma once

#include <vector>

#include <geo/model.h>
#include <geo/scene.h>
#include <geo/vec3.h>

namespace plnr::geo::csg {

// Boolean CSG between two solids: a fresh-build pipeline producing a polygon soup (MeshSpec);
// nothing here mutates a Model or a Scene. No exceptions -- failures come back as Result::ok = false.

enum class Op {
    Union,      // A OR B
    Subtract,   // a MINUS b; kept cutter fragments come out FLIPPED (they bound the cavity).
    Intersect,  // A AND B
};

// One boolean operand: a solid plus the transform into the shared frame. toWorld may mirror.
struct Operand {
    const Model* model{};
    Transform toWorld;  // operand geometry -> common world frame
};

// Output polygon in WORLD coordinates. Faces may be unmerged convex fragments with T-vertices;
// re-merging is mergeCoplanarFaces's job. No relation between source faces and output face count.
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

// ok = false only for structural failure: a null Operand::model, an operand with no usable triangle,
// or a breach of apply()'s BSP guards. An EMPTY result is an ordinary success (ok = true, 0 faces).
struct Result {
    bool ok{};
    MeshSpec mesh;
};

// Boolean of a and b in the common world frame (BSP clipping). Source faces are visited in ascending
// id order for deterministic output; plane classification uses kPlaneTol. Aborts with ok = false past
// kMaxBspDepth (512) tree levels or kMaxPolygons (250000) fragments.
Result apply(const Operand& a, const Operand& b, Op op);

// Signed divergence-theorem volume of a polygon soup, fan-triangulated from loop[0]: positive for an
// outward-oriented closed shell, negative for inward normals. Malformed loops are skipped.
double meshVolume(const MeshSpec& mesh);

// Opt-in post-processing on apply()'s raw fragment soup.

// Heals cross-face T-vertices, then merges coplanar same-provenance fragments into maximal SIMPLE
// polygons (volume-preserving, deterministic). Every returned loop has >= 3 distinct indices, and
// ALL boundary vertices are kept -- dropping collinear ones reopens a T-crack on a neighbouring
// face's plane. A degenerate group falls back to its original healed fragments instead of failing.
MeshSpec mergeCoplanarFaces(const MeshSpec& mesh);

// True when every undirected edge is used by exactly two faces in opposite directions. Judged
// VERBATIM -- no welding or tolerance, so a raw apply() soup with T-vertices is not watertight.
// A mesh with no faces is vacuously watertight; any malformed loop makes it not watertight.
bool meshIsWatertight(const MeshSpec& mesh);

// Union of a and b with every enclosed-void boundary removed, so only faces reachable from outside
// remain (result is MERGED). Classified per SHELL (faces joined transitively by a shared vertex):
// a shell survives unless contained in another, tested by ray cast with a deterministic jitter table
// (never random) on near-degenerate hits; an unresolved cast keeps the shell.
Result outerShell(const Operand& a, const Operand& b);

// Up to three solids from one operand pair, each MERGED. An empty piece is an ordinary success;
// ok = false only when one of the three underlying apply() calls fails structurally.
struct SplitResult {
    bool ok{};
    MeshSpec aMinusB;
    MeshSpec bMinusA;
    MeshSpec aIntersectB;
};

SplitResult split(const Operand& a, const Operand& b);

}  // namespace plnr::geo::csg
