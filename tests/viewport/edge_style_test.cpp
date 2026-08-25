// Tests the pure, Qt/domain-free classifyEdge/depthBandIndex/
// classifyAndBucketEdges helpers behind the edge silhouette/profile
// classification and depth-banding pipeline (src/app/viewport/edge_style.h).

#include "viewport/edge_style.h"

#include <gtest/gtest.h>

namespace plnr::viewport {
namespace {

// ---- classifyEdge: cube-corner cases ---------------------------------------
// Fixture: vertical edge (0,0,0)-(0,0,1) at a cube's outer CONVEX corner,
// with faces F1 (-X normal) and F2 (-Y normal) meeting there. Eye position
// controls whether both faces read front-facing (Interior) or only one does (Silhouette).

constexpr geo::Vec3 kCubeEdgeA{0.0, 0.0, 0.0};
constexpr geo::Vec3 kCubeEdgeB{0.0, 0.0, 1.0};
constexpr geo::Vec3 kFaceNormalLeft{-1.0, 0.0, 0.0};
constexpr geo::Vec3 kFaceNormalFront{0.0, -1.0, 0.0};

TEST(EdgeStyleClassify, FrontFacingPair_IsInteriorNotProfile) {
    // Eye at (-5,-5,0.5): both F1/F2 face toward it (same sign) -- an
    // ordinary convex-corner edge, no silhouette.
    const EdgeAdjacency edge{kCubeEdgeA, kCubeEdgeB, kFaceNormalLeft, kFaceNormalFront};
    const EdgeClass cls = classifyEdge(edge, geo::Vec3{-5.0, -5.0, 0.5});
    EXPECT_EQ(cls, EdgeClass::Interior);
    EXPECT_FALSE(isProfileWeight(cls));
}

TEST(EdgeStyleClassify, SilhouettePair_IsProfileWeight) {
    // Eye at (10,-5,0.5): F1 (-X normal) now faces AWAY from the eye while
    // F2 (-Y normal) still faces toward it -- the two faces disagree, so
    // this edge is the view-dependent silhouette boundary between them.
    const EdgeAdjacency edge{kCubeEdgeA, kCubeEdgeB, kFaceNormalLeft, kFaceNormalFront};
    const EdgeClass cls = classifyEdge(edge, geo::Vec3{10.0, -5.0, 0.5});
    EXPECT_EQ(cls, EdgeClass::Silhouette);
    EXPECT_TRUE(isProfileWeight(cls));
}

TEST(EdgeStyleClassify, ExactlyOneFace_IsBoundaryProfile) {
    // A mesh boundary edge is always an outline, regardless of eye position
    // -- try two very different eyes and confirm both agree.
    const EdgeAdjacency edge{kCubeEdgeA, kCubeEdgeB, kFaceNormalLeft, std::nullopt};
    EXPECT_EQ(classifyEdge(edge, geo::Vec3{-5.0, -5.0, 0.5}), EdgeClass::Profile);
    EXPECT_EQ(classifyEdge(edge, geo::Vec3{50.0, 50.0, 50.0}), EdgeClass::Profile);
    EXPECT_TRUE(isProfileWeight(EdgeClass::Profile));
}

TEST(EdgeStyleClassify, NoFaces_IsWireAtProfileWeight) {
    // No adjacent face at all (a standalone edge) -- matches the reference modeler
    // treating lone edges at profile weight.
    const EdgeAdjacency edge{kCubeEdgeA, kCubeEdgeB, std::nullopt, std::nullopt};
    const EdgeClass cls = classifyEdge(edge, geo::Vec3{-5.0, -5.0, 0.5});
    EXPECT_EQ(cls, EdgeClass::Wire);
    EXPECT_TRUE(isProfileWeight(cls));
}

TEST(EdgeStyleClassify, DegenerateNormalSafety_ZeroNormalDoesNotCrashOrNaN) {
    // Zero-length normal (degenerate/zero-area face): dot==0, sign 0,
    // compares as "different" from a real face's +-1. Guarantee under test:
    // this path never divides/normalizes, so no UB/NaN on degenerate input.
    const EdgeAdjacency edge{kCubeEdgeA, kCubeEdgeB, geo::Vec3{0.0, 0.0, 0.0}, kFaceNormalFront};
    const EdgeClass cls = classifyEdge(edge, geo::Vec3{-5.0, -5.0, 0.5});
    EXPECT_EQ(cls, EdgeClass::Silhouette);
    EXPECT_TRUE(isProfileWeight(cls));
    // Both normals degenerate: sign 0 == sign 0 -> Interior, still no crash.
    const EdgeAdjacency bothDegenerate{kCubeEdgeA, kCubeEdgeB, geo::Vec3{0.0, 0.0, 0.0}, geo::Vec3{0.0, 0.0, 0.0}};
    EXPECT_EQ(classifyEdge(bothDegenerate, geo::Vec3{-5.0, -5.0, 0.5}), EdgeClass::Interior);
}

// ---- depthBandIndex ---------------------------------------------------------

TEST(EdgeStyleDepthBand, BandCountAtMostOne_AlwaysZero) {
    EXPECT_EQ(depthBandIndex(5.0, 0.0, 10.0, 1), 0);
    EXPECT_EQ(depthBandIndex(5.0, 0.0, 10.0, 0), 0);
}

TEST(EdgeStyleDepthBand, DegenerateRange_AlwaysNearestBand) {
    EXPECT_EQ(depthBandIndex(5.0, 5.0, 5.0, 3), 0);
    EXPECT_EQ(depthBandIndex(100.0, 5.0, 5.0, 3), 0);  // even a distance outside the (degenerate) range
}

TEST(EdgeStyleDepthBand, ThreeBands_NearestToFarthest) {
    // [0, 12] split into 3 -- band width 4.
    EXPECT_EQ(depthBandIndex(0.0, 0.0, 12.0, 3), 0);
    EXPECT_EQ(depthBandIndex(5.0, 0.0, 12.0, 3), 1);
    EXPECT_EQ(depthBandIndex(11.0, 0.0, 12.0, 3), 2);
    EXPECT_EQ(depthBandIndex(12.0, 0.0, 12.0, 3), 2);  // exactly maxDist -- last band, not out of range
}

TEST(EdgeStyleDepthBand, OutOfRangeDistance_Clamped) {
    EXPECT_EQ(depthBandIndex(-5.0, 0.0, 12.0, 3), 0);
    EXPECT_EQ(depthBandIndex(100.0, 0.0, 12.0, 3), 2);
}

// ---- classifyAndBucketEdges --------------------------------------------------

TEST(EdgeStyleBucket, EmptyInput_ReturnsEmptyBucketsSizedToBandCount) {
    const BandedEdgeBuckets result = classifyAndBucketEdges({}, geo::Vec3{}, 3);
    EXPECT_TRUE(result.profileVerts.empty());
    ASSERT_EQ(result.bandVerts.size(), 3u);
    for (const auto& band : result.bandVerts) EXPECT_TRUE(band.empty());
}

TEST(EdgeStyleBucket, NonPositiveBandCount_ReturnsNoBandBuffers) {
    const std::vector<EdgeAdjacency> edges{{kCubeEdgeA, kCubeEdgeB, std::nullopt, std::nullopt}};
    const BandedEdgeBuckets result = classifyAndBucketEdges(edges, geo::Vec3{}, 0);
    EXPECT_TRUE(result.bandVerts.empty());
}

TEST(EdgeStyleBucket, MixedSet_ProfileAndBandedInteriorSeparateCorrectly) {
    const geo::Vec3 eye{-100.0, -100.0, 0.0};

    // Two Interior edges (same front-facing-pair shape as
    // FrontFacingPair_IsInteriorNotProfile): edgeNear close to eye, edgeFar
    // close to the world origin, much farther away.
    const EdgeAdjacency edgeNear{geo::Vec3{-90.0, -95.0, 0.0}, geo::Vec3{-90.0, -95.0, 1.0}, kFaceNormalLeft,
                                  kFaceNormalFront};
    const EdgeAdjacency edgeFar{kCubeEdgeA, kCubeEdgeB, kFaceNormalLeft, kFaceNormalFront};
    ASSERT_EQ(classifyEdge(edgeNear, eye), EdgeClass::Interior);
    ASSERT_EQ(classifyEdge(edgeFar, eye), EdgeClass::Interior);

    const EdgeAdjacency boundary{kCubeEdgeA, kCubeEdgeB, kFaceNormalLeft, std::nullopt};
    const EdgeAdjacency wire{kCubeEdgeA, kCubeEdgeB, std::nullopt, std::nullopt};

    const std::vector<EdgeAdjacency> edges{edgeNear, edgeFar, boundary, wire};
    const BandedEdgeBuckets result = classifyAndBucketEdges(edges, eye, /*bandCount=*/2);

    // boundary + wire -> profileVerts, 6 floats each (one edge = 2 verts * 3 floats).
    EXPECT_EQ(result.profileVerts.size(), 12u);

    ASSERT_EQ(result.bandVerts.size(), 2u);
    // Nearest edge -> band 0 (thickest), farthest -> band 1 (thinnest) --
    // each band holds exactly one edge's worth of floats.
    EXPECT_EQ(result.bandVerts[0].size(), 6u);
    EXPECT_EQ(result.bandVerts[1].size(), 6u);
}

}  // namespace
}  // namespace plnr::viewport
