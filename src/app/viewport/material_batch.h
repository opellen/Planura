#pragma once

#include <cmath>
#include <cstddef>
#include <functional>
#include <vector>

#include <geo/model.h>  // geo::Id
#include <geo/vec3.h>   // geo::Vec3

// Pure logic behind material-batched face rendering: per-face material
// resolution, grouping face-VBO triangles into per-material draw ranges, and
// opaque/transparent partitioning plus back-to-front ordering. Qt/domain-free.
namespace plnr::viewport {

// ---- Material resolution ---------------------------------------------------
// the reference modeler inheritance: a nonzero slot wins; 0 (unpainted) falls back to the
// nearest painted ancestor's front slot, for a face's front AND back alike.

inline geo::Id resolveSlot(geo::Id ownValue, geo::Id inheritedMaterialId) {
    return ownValue != 0 ? ownValue : inheritedMaterialId;
}

struct ResolvedFaceMaterial {
    geo::Id frontMaterialId{};
    geo::Id backMaterialId{};
};

// A face's own {front,back} slots (0/0 = no MaterialAssignment), each
// resolved against inheritedMaterialId.
inline ResolvedFaceMaterial resolveFaceMaterial(geo::Id ownFrontMaterialId, geo::Id ownBackMaterialId,
                                                 geo::Id inheritedMaterialId) {
    return {resolveSlot(ownFrontMaterialId, inheritedMaterialId), resolveSlot(ownBackMaterialId, inheritedMaterialId)};
}

// The inheritedMaterialId passed down to one Instance's children. Called once
// per Instance as the walk recurses, so chains resolve to the nearest ancestor.
inline geo::Id nextInheritedMaterialId(geo::Id currentInheritedMaterialId, geo::Id instanceOwnFrontMaterialId) {
    return resolveSlot(instanceOwnFrontMaterialId, currentInheritedMaterialId);
}

// ---- Range bucketing -------------------------------------------------------

inline geo::Vec3 triangleCentroid(const geo::Vec3& a, const geo::Vec3& b, const geo::Vec3& c) {
    return (a + b + c) * (1.0 / 3.0);
}

// One triangle's resolved material pair + world-space centroid, built in the
// SAME order triangles land in faceTris (triangle i = faceTris[9*i .. 9*i+8]).
struct TriangleMaterialKey {
    geo::Id frontMaterialId{};
    geo::Id backMaterialId{};
    geo::Vec3 centroid;
};

// One contiguous draw range over the face VBO: first/count in VERTEX units (3
// per triangle), for glDrawArrays(GL_TRIANGLES, first, count). centroid is the
// unweighted average of member centroids, used only by the transparent sort.
struct MaterialRange {
    int first{};
    int count{};
    geo::Id frontMaterialId{};
    geo::Id backMaterialId{};
    geo::Vec3 centroid;
};

struct BucketResult {
    // order[i] = the original triangle index landing at output position i;
    // permute the raw per-triangle vertex data by it to match ranges.
    std::vector<std::size_t> order;
    std::vector<MaterialRange> ranges;
};

// Groups triangle indices into contiguous MaterialRange entries keyed by
// (frontMaterialId, backMaterialId), in stable first-appearance order: one
// contiguous run per distinct pair. Empty keys returns an empty result.
BucketResult bucketTrianglesByMaterial(const std::vector<TriangleMaterialKey>& keys);

// ---- Opaque/transparent partition + transparent back-to-front ordering ---

struct PartitionedRanges {
    std::vector<MaterialRange> opaque;       // any order -- opaque ranges never blend
    std::vector<MaterialRange> transparent;  // unordered -- see orderTransparentRangesBackToFront
};

// Opaque = materialOpacity(frontMaterialId) >= 1, transparent = < 1; each
// partition keeps its relative input order. materialOpacity must return 1.0
// for the id-0 sentinel and for unrecognized ids -- no special-casing here.
PartitionedRanges partitionRangesByOpacity(std::vector<MaterialRange> ranges,
                                            const std::function<double(geo::Id)>& materialOpacity);

// Sorts back-to-front by centroid distance from eye, farthest first. Called
// once per rebuild, not per frame, so a moving camera can leave it stale.
void orderTransparentRangesBackToFront(std::vector<MaterialRange>& ranges, const geo::Vec3& eye);

// ---- UV generation --------------------------------------------------------
// Projection math only. The caller supplies tileW/tileH and gives a real basis
// only to faces whose resolved FRONT-slot material is textured; others {0,0}.

// One face's in-plane UV basis, derived from its world-space normal. The basis
// is deterministic, so coplanar faces sharing a normal get the SAME basis --
// required for tiling continuity across the model.
struct FaceUvBasis {
    geo::Vec3 uAxis;
    geo::Vec3 vAxis;
};

// |n.z| threshold at which faceUvBasis swaps its helper axis from worldZ to
// worldX. A tight numeric-degeneracy guard, not a loose visual threshold: it
// fires only when cross(n, worldZ) is too small to normalize safely.
inline constexpr double kUvBasisZAlignTol = geo::kMergeTol;

// uAxis = normalize(cross(n, worldZ)), helper axis swapped to worldX when the
// normal is within kUvBasisZAlignTol of vertical; vAxis = cross(n, uAxis).
// A degenerate (near-zero) normal yields zero axes, never NaN.
inline FaceUvBasis faceUvBasis(const geo::Vec3& normal) {
    const geo::Vec3 n = geo::normalized(normal);
    constexpr geo::Vec3 kWorldZ{0.0, 0.0, 1.0};
    constexpr geo::Vec3 kWorldX{1.0, 0.0, 0.0};
    const geo::Vec3 helper = std::fabs(n.z) >= 1.0 - kUvBasisZAlignTol ? kWorldX : kWorldZ;
    const geo::Vec3 uAxis = geo::normalized(geo::cross(n, helper));
    const geo::Vec3 vAxis = geo::cross(n, uAxis);
    return {uAxis, vAxis};
}

struct Uv {
    double u{};
    double v{};
};

// World position p projected onto the basis and scaled so the pattern repeats
// every tileW x tileH units. tileW/tileH at or below geo::kEps fall back to 1.0.
inline Uv faceVertexUv(const FaceUvBasis& basis, const geo::Vec3& p, double tileW, double tileH) {
    const double safeTileW = tileW > geo::kEps ? tileW : 1.0;
    const double safeTileH = tileH > geo::kEps ? tileH : 1.0;
    return {geo::dot(p, basis.uAxis) / safeTileW, geo::dot(p, basis.vAxis) / safeTileH};
}

// ---- Per-face UV transform -------------------------------------------------
// Domain-free mirror of events::UvTransform (agent/events.h); the caller
// converts field-by-field before calling applyUvTransform.

// offsetU/offsetV: translation in tile-scaled UV units (1.0 = one full tile).
// rotationRad: about the UV origin. scaleU/scaleV: applied last.
struct UvTransform {
    double offsetU{};
    double offsetV{};
    double rotationRad{};
    double scaleU{1.0};
    double scaleV{1.0};
};

// uv' = S(1/scaleU, 1/scaleV) * R(-rotationRad) * (uv - offset) -- the inverse
// of the texture's own transform. scaleU/scaleV at or below geo::kEps use 1.0.
inline Uv applyUvTransform(const Uv& uv, const UvTransform& t) {
    const double du = uv.u - t.offsetU;
    const double dv = uv.v - t.offsetV;
    const double cosR = std::cos(-t.rotationRad);
    const double sinR = std::sin(-t.rotationRad);
    const double ru = du * cosR - dv * sinR;
    const double rv = du * sinR + dv * cosR;
    const double safeScaleU = std::fabs(t.scaleU) > geo::kEps ? t.scaleU : 1.0;
    const double safeScaleV = std::fabs(t.scaleV) > geo::kEps ? t.scaleV : 1.0;
    return {ru / safeScaleU, rv / safeScaleV};
}

}  // namespace plnr::viewport
