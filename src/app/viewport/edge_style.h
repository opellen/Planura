#pragma once

#include <cstddef>
#include <optional>
#include <vector>

#include <geo/vec3.h>  // geo::Vec3, geo::dot, geo::distance, geo::kEps

// Edge silhouette/profile classification and eye-distance depth banding for
// the edge-style rendering pipeline. Qt/domain-free (geo:: + std only).
namespace plnr::viewport {

// ---- Silhouette/profile classification -------------------------------------

// World-space endpoints plus each adjacent face's world-space normal
// (nullopt = no face). Normals need not be normalized: only the sign of their
// dot product with the eye vector matters.
struct EdgeAdjacency {
    geo::Vec3 a;
    geo::Vec3 b;
    std::optional<geo::Vec3> normalA;
    std::optional<geo::Vec3> normalB;
};

// Profile/Silhouette/Wire all render at the same thicker width; only Interior
// renders thin.
enum class EdgeClass {
    Interior,    // two adjacent faces, same side faces the eye
    Profile,     // exactly one adjacent face (a mesh boundary edge)
    Silhouette,  // two adjacent faces that disagree on which side faces the eye
    Wire,        // no adjacent face at all
};

constexpr bool isProfileWeight(EdgeClass cls) {
    return cls != EdgeClass::Interior;
}

// No adjacent face -> Wire; exactly one -> Profile; two -> Silhouette when
// sign(dot(normal, mid-eye)) differs between them, else Interior.
// Degenerate (near-zero) normals compare via sign 0, distinct from +-1.
inline EdgeClass classifyEdge(const EdgeAdjacency& edge, const geo::Vec3& eye) {
    const bool hasA = edge.normalA.has_value();
    const bool hasB = edge.normalB.has_value();
    if (!hasA && !hasB) return EdgeClass::Wire;
    if (hasA != hasB) return EdgeClass::Profile;

    const geo::Vec3 mid = (edge.a + edge.b) * 0.5;
    const geo::Vec3 toMid = mid - eye;
    const auto signOf = [](double v) -> int { return (v > 0.0) - (v < 0.0); };
    const int sA = signOf(geo::dot(*edge.normalA, toMid));
    const int sB = signOf(geo::dot(*edge.normalB, toMid));
    return sA != sB ? EdgeClass::Silhouette : EdgeClass::Interior;
}

// ---- Depth banding ----------------------------------------------------------

// 0 = nearest (thickest) .. bandCount-1 = farthest; distance is clamped into
// [minDist, maxDist] first. Degenerate range or bandCount <= 1 returns 0.
inline int depthBandIndex(double distance, double minDist, double maxDist, int bandCount) {
    if (bandCount <= 1) return 0;
    const double range = maxDist - minDist;
    if (range <= geo::kEps) return 0;
    const double clamped = distance < minDist ? minDist : (distance > maxDist ? maxDist : distance);
    const double t = (clamped - minDist) / range;  // in [0, 1]
    int band = static_cast<int>(t * bandCount);
    if (band >= bandCount) band = bandCount - 1;  // t == 1.0 edge case
    return band;
}

// ---- Classify + bucket a whole edge set -------------------------------------

// Appends one edge as 6 floats (xyz/xyz), the position-only GL_LINES layout.
inline void appendEdgeVerts(std::vector<float>& out, const geo::Vec3& a, const geo::Vec3& b) {
    out.push_back(static_cast<float>(a.x));
    out.push_back(static_cast<float>(a.y));
    out.push_back(static_cast<float>(a.z));
    out.push_back(static_cast<float>(b.x));
    out.push_back(static_cast<float>(b.y));
    out.push_back(static_cast<float>(b.z));
}

// Flat position-only buffers (appendEdgeVerts layout), 6 floats per edge.
// bandVerts has exactly bandCount entries; index 0 = nearest/thickest.
struct BandedEdgeBuckets {
    std::vector<float> profileVerts;
    std::vector<std::vector<float>> bandVerts;
};

// Profile-weight edges go to profileVerts; Interior edges bucket by
// depthBandIndex over the min/max eye distance among INTERIOR edges only.
// Empty edges or bandCount <= 0 gives an all-empty result.
BandedEdgeBuckets classifyAndBucketEdges(const std::vector<EdgeAdjacency>& edges, const geo::Vec3& eye, int bandCount);

}  // namespace plnr::viewport
