// Tests the pure, Qt/domain-free helpers behind material-batched face
// rendering (resolution, range bucketing, opaque/transparent partition,
// back-to-front ordering) and UV generation (faceUvBasis/faceVertexUv).

#include "viewport/material_batch.h"

#include <cmath>

#include <gtest/gtest.h>

namespace plnr::viewport {
namespace {

// ---- Material resolution ---------------------------------------------------

TEST(MaterialBatch, ResolveFaceMaterial_OwnOverrideWinsWhenNonzero) {
    const ResolvedFaceMaterial resolved = resolveFaceMaterial(/*ownFront=*/5, /*ownBack=*/7, /*inherited=*/99);
    EXPECT_EQ(resolved.frontMaterialId, 5u);
    EXPECT_EQ(resolved.backMaterialId, 7u);
}

TEST(MaterialBatch, ResolveFaceMaterial_FallsBackToInheritedPerSlot) {
    // Front unpainted (0), back painted -- each slot resolves independently.
    const ResolvedFaceMaterial resolved = resolveFaceMaterial(/*ownFront=*/0, /*ownBack=*/7, /*inherited=*/99);
    EXPECT_EQ(resolved.frontMaterialId, 99u);
    EXPECT_EQ(resolved.backMaterialId, 7u);
}

TEST(MaterialBatch, ResolveFaceMaterial_NoOverrideNoInherited_IsSentinelZero) {
    const ResolvedFaceMaterial resolved = resolveFaceMaterial(/*ownFront=*/0, /*ownBack=*/0, /*inherited=*/0);
    EXPECT_EQ(resolved.frontMaterialId, 0u);
    EXPECT_EQ(resolved.backMaterialId, 0u);
}

TEST(MaterialBatch, NextInheritedMaterialId_PaintedInstanceOverridesChain) {
    EXPECT_EQ(nextInheritedMaterialId(/*current=*/0, /*instanceOwnFront=*/42), 42u);
}

TEST(MaterialBatch, NextInheritedMaterialId_UnpaintedInstancePassesThroughChain) {
    EXPECT_EQ(nextInheritedMaterialId(/*current=*/42, /*instanceOwnFront=*/0), 42u);
}

TEST(MaterialBatch, NestedInstanceChain_NearestPaintedAncestorWins) {
    // Root (0) -> group A (painted 10) -> group B (unpainted) -> face
    // (unpainted): the face should inherit group A's material (10), skipping
    // straight through unpainted group B.
    const geo::Id afterA = nextInheritedMaterialId(/*current=*/0, /*instanceOwnFront=*/10);
    const geo::Id afterB = nextInheritedMaterialId(afterA, /*instanceOwnFront=*/0);
    const ResolvedFaceMaterial face = resolveFaceMaterial(/*ownFront=*/0, /*ownBack=*/0, afterB);
    EXPECT_EQ(afterA, 10u);
    EXPECT_EQ(afterB, 10u);
    EXPECT_EQ(face.frontMaterialId, 10u);
    EXPECT_EQ(face.backMaterialId, 10u);
}

TEST(MaterialBatch, NestedInstanceChain_DeeperPaintOverridesShallowerAncestor) {
    // Root (0) -> group A (painted 10) -> group C (painted 20) -> face
    // (unpainted): the face should inherit the NEAREST ancestor, group C's
    // material (20), not group A's.
    const geo::Id afterA = nextInheritedMaterialId(/*current=*/0, /*instanceOwnFront=*/10);
    const geo::Id afterC = nextInheritedMaterialId(afterA, /*instanceOwnFront=*/20);
    const ResolvedFaceMaterial face = resolveFaceMaterial(/*ownFront=*/0, /*ownBack=*/0, afterC);
    EXPECT_EQ(afterC, 20u);
    EXPECT_EQ(face.frontMaterialId, 20u);
}

TEST(MaterialBatch, NestedInstanceChain_FaceOwnOverrideBeatsEveryAncestor) {
    const geo::Id afterA = nextInheritedMaterialId(/*current=*/0, /*instanceOwnFront=*/10);
    const ResolvedFaceMaterial face = resolveFaceMaterial(/*ownFront=*/77, /*ownBack=*/0, afterA);
    EXPECT_EQ(face.frontMaterialId, 77u);   // own override wins
    EXPECT_EQ(face.backMaterialId, 10u);    // back still inherits the ancestor
}

// ---- Range bucketing --------------------------------------------------------

TEST(MaterialBatch, BucketTrianglesByMaterial_EmptyInput) {
    const BucketResult result = bucketTrianglesByMaterial({});
    EXPECT_TRUE(result.order.empty());
    EXPECT_TRUE(result.ranges.empty());
}

TEST(MaterialBatch, BucketTrianglesByMaterial_SingleMaterial_OneContiguousRange) {
    const std::vector<TriangleMaterialKey> keys = {
        {1, 0, geo::Vec3{0.0, 0.0, 0.0}},
        {1, 0, geo::Vec3{2.0, 0.0, 0.0}},
        {1, 0, geo::Vec3{4.0, 0.0, 0.0}},
    };
    const BucketResult result = bucketTrianglesByMaterial(keys);
    ASSERT_EQ(result.ranges.size(), 1u);
    EXPECT_EQ(result.ranges[0].first, 0);
    EXPECT_EQ(result.ranges[0].count, 9);  // 3 triangles * 3 vertices
    EXPECT_EQ(result.ranges[0].frontMaterialId, 1u);
    EXPECT_EQ(result.ranges[0].backMaterialId, 0u);
    EXPECT_DOUBLE_EQ(result.ranges[0].centroid.x, 2.0);  // average of 0,2,4
    ASSERT_EQ(result.order.size(), 3u);
    EXPECT_EQ(result.order, (std::vector<std::size_t>{0, 1, 2}));
}

TEST(MaterialBatch, BucketTrianglesByMaterial_InterleavedKeys_StayContiguousInOutput) {
    // Materials interleaved in the walk order (A, B, A, B) must still end up
    // as two CONTIGUOUS ranges -- the underlying VBO gets one contiguous run
    // per material pair regardless of scene-walk order.
    const std::vector<TriangleMaterialKey> keys = {
        {1, 0, geo::Vec3{0.0, 0.0, 0.0}},  // triangle 0: material A
        {2, 0, geo::Vec3{0.0, 0.0, 0.0}},  // triangle 1: material B
        {1, 0, geo::Vec3{0.0, 0.0, 0.0}},  // triangle 2: material A
        {2, 0, geo::Vec3{0.0, 0.0, 0.0}},  // triangle 3: material B
    };
    const BucketResult result = bucketTrianglesByMaterial(keys);
    ASSERT_EQ(result.ranges.size(), 2u);

    // Bucket A (first-appearance order) comes first, contiguous.
    EXPECT_EQ(result.ranges[0].frontMaterialId, 1u);
    EXPECT_EQ(result.ranges[0].first, 0);
    EXPECT_EQ(result.ranges[0].count, 6);  // triangles 0 and 2

    // Bucket B comes second, contiguous, starting right after A ends.
    EXPECT_EQ(result.ranges[1].frontMaterialId, 2u);
    EXPECT_EQ(result.ranges[1].first, 6);
    EXPECT_EQ(result.ranges[1].count, 6);  // triangles 1 and 3

    // order must place both A-triangles (0, 2) adjacent, then both
    // B-triangles (1, 3) adjacent -- exactly what makes the ranges above
    // valid contiguous slices of a REORDERED vertex buffer.
    EXPECT_EQ(result.order, (std::vector<std::size_t>{0, 2, 1, 3}));
}

TEST(MaterialBatch, BucketTrianglesByMaterial_DistinctFrontBackPairsAreSeparateRanges) {
    // Same frontMaterialId but different backMaterialId must NOT merge --
    // the key is the (front,back) PAIR.
    const std::vector<TriangleMaterialKey> keys = {
        {1, 0, geo::Vec3{}},
        {1, 5, geo::Vec3{}},
    };
    const BucketResult result = bucketTrianglesByMaterial(keys);
    ASSERT_EQ(result.ranges.size(), 2u);
    EXPECT_EQ(result.ranges[0].backMaterialId, 0u);
    EXPECT_EQ(result.ranges[1].backMaterialId, 5u);
}

TEST(MaterialBatch, BucketTrianglesByMaterial_SentinelZeroIsAnOrdinaryKey) {
    const std::vector<TriangleMaterialKey> keys = {
        {0, 0, geo::Vec3{}},
        {0, 0, geo::Vec3{}},
    };
    const BucketResult result = bucketTrianglesByMaterial(keys);
    ASSERT_EQ(result.ranges.size(), 1u);
    EXPECT_EQ(result.ranges[0].frontMaterialId, 0u);
    EXPECT_EQ(result.ranges[0].backMaterialId, 0u);
    EXPECT_EQ(result.ranges[0].count, 6);
}

// ---- Opaque/transparent partition -------------------------------------------

TEST(MaterialBatch, PartitionRangesByOpacity_SentinelAndOpaqueMaterialBothLandOpaque) {
    std::vector<MaterialRange> ranges;
    ranges.push_back(MaterialRange{0, 3, /*front=*/0, /*back=*/0, geo::Vec3{}});    // sentinel
    ranges.push_back(MaterialRange{3, 3, /*front=*/1, /*back=*/0, geo::Vec3{}});    // opaque material

    const auto opacityOf = [](geo::Id id) -> double {
        if (id == 0) return 1.0;  // sentinel contract: caller answers 1.0 for id 0
        if (id == 1) return 1.0;  // an actual opacity-1 material
        return 1.0;
    };
    const PartitionedRanges result = partitionRangesByOpacity(ranges, opacityOf);
    EXPECT_EQ(result.opaque.size(), 2u);
    EXPECT_TRUE(result.transparent.empty());
}

TEST(MaterialBatch, PartitionRangesByOpacity_SplitsByFrontMaterialOpacity) {
    std::vector<MaterialRange> ranges;
    ranges.push_back(MaterialRange{0, 3, /*front=*/1, /*back=*/0, geo::Vec3{}});  // opacity 1.0
    ranges.push_back(MaterialRange{3, 3, /*front=*/2, /*back=*/0, geo::Vec3{}});  // opacity 0.5

    const auto opacityOf = [](geo::Id id) -> double { return id == 2 ? 0.5 : 1.0; };
    const PartitionedRanges result = partitionRangesByOpacity(ranges, opacityOf);
    ASSERT_EQ(result.opaque.size(), 1u);
    ASSERT_EQ(result.transparent.size(), 1u);
    EXPECT_EQ(result.opaque[0].frontMaterialId, 1u);
    EXPECT_EQ(result.transparent[0].frontMaterialId, 2u);
}

TEST(MaterialBatch, PartitionRangesByOpacity_PreservesRelativeOrderWithinEachPartition) {
    std::vector<MaterialRange> ranges;
    ranges.push_back(MaterialRange{0, 3, /*front=*/2, /*back=*/0, geo::Vec3{}});  // transparent
    ranges.push_back(MaterialRange{3, 3, /*front=*/1, /*back=*/0, geo::Vec3{}});  // opaque
    ranges.push_back(MaterialRange{6, 3, /*front=*/3, /*back=*/0, geo::Vec3{}});  // transparent

    const auto opacityOf = [](geo::Id id) -> double { return (id == 2 || id == 3) ? 0.5 : 1.0; };
    const PartitionedRanges result = partitionRangesByOpacity(ranges, opacityOf);
    ASSERT_EQ(result.transparent.size(), 2u);
    EXPECT_EQ(result.transparent[0].frontMaterialId, 2u);
    EXPECT_EQ(result.transparent[1].frontMaterialId, 3u);
}

// ---- Transparent back-to-front centroid ordering ----------------------------

TEST(MaterialBatch, OrderTransparentRangesBackToFront_FarthestFirst) {
    std::vector<MaterialRange> ranges;
    ranges.push_back(MaterialRange{0, 3, 1, 0, geo::Vec3{1.0, 0.0, 0.0}});    // near
    ranges.push_back(MaterialRange{3, 3, 2, 0, geo::Vec3{10.0, 0.0, 0.0}});   // far
    ranges.push_back(MaterialRange{6, 3, 3, 0, geo::Vec3{5.0, 0.0, 0.0}});    // mid

    orderTransparentRangesBackToFront(ranges, geo::Vec3{0.0, 0.0, 0.0});

    ASSERT_EQ(ranges.size(), 3u);
    EXPECT_EQ(ranges[0].frontMaterialId, 2u);  // farthest first
    EXPECT_EQ(ranges[1].frontMaterialId, 3u);
    EXPECT_EQ(ranges[2].frontMaterialId, 1u);  // nearest last
}

TEST(MaterialBatch, OrderTransparentRangesBackToFront_ReordersRelativeToMovedEye) {
    std::vector<MaterialRange> ranges;
    ranges.push_back(MaterialRange{0, 3, 1, 0, geo::Vec3{0.0, 0.0, 0.0}});
    ranges.push_back(MaterialRange{3, 3, 2, 0, geo::Vec3{10.0, 0.0, 0.0}});

    // Eye near the second range's centroid -- the FIRST range is now
    // farthest and should sort first.
    orderTransparentRangesBackToFront(ranges, geo::Vec3{9.0, 0.0, 0.0});

    ASSERT_EQ(ranges.size(), 2u);
    EXPECT_EQ(ranges[0].frontMaterialId, 1u);
    EXPECT_EQ(ranges[1].frontMaterialId, 2u);
}

// ---- UV generation -----------------------------------------------------

TEST(MaterialBatch, FaceUvBasis_OrthonormalForArbitraryNormal) {
    // (1,2,3) is neither axis-aligned nor near-vertical -- exercises the
    // ordinary (non-fallback) worldZ-helper path.
    const FaceUvBasis basis = faceUvBasis(geo::Vec3{1.0, 2.0, 3.0});
    EXPECT_NEAR(geo::length(basis.uAxis), 1.0, 1e-9);
    EXPECT_NEAR(geo::length(basis.vAxis), 1.0, 1e-9);
    EXPECT_NEAR(geo::dot(basis.uAxis, basis.vAxis), 0.0, 1e-9);
    const geo::Vec3 n = geo::normalized(geo::Vec3{1.0, 2.0, 3.0});
    EXPECT_NEAR(geo::dot(n, basis.uAxis), 0.0, 1e-9);
    EXPECT_NEAR(geo::dot(n, basis.vAxis), 0.0, 1e-9);
}

TEST(MaterialBatch, FaceUvBasis_HorizontalNormalUsesWorldZHelper) {
    // normal = worldX: |n.z| == 0, well below the fallback threshold, so the
    // helper stays worldZ -- uAxis = normalize(cross(worldX, worldZ)),
    // vAxis = cross(worldX, uAxis).
    const FaceUvBasis basis = faceUvBasis(geo::Vec3{1.0, 0.0, 0.0});
    EXPECT_NEAR(basis.uAxis.x, 0.0, 1e-9);
    EXPECT_NEAR(basis.uAxis.y, -1.0, 1e-9);
    EXPECT_NEAR(basis.uAxis.z, 0.0, 1e-9);
    EXPECT_NEAR(basis.vAxis.x, 0.0, 1e-9);
    EXPECT_NEAR(basis.vAxis.y, 0.0, 1e-9);
    EXPECT_NEAR(basis.vAxis.z, -1.0, 1e-9);
}

TEST(MaterialBatch, FaceUvBasis_VerticalNormalFallsBackToWorldXHelper) {
    // normal = worldZ exactly: |n.z| == 1 triggers the documented fallback
    // (cross(n, worldZ) would otherwise be the zero vector) -- helper
    // becomes worldX, uAxis = normalize(cross(worldZ, worldX)).
    const FaceUvBasis basis = faceUvBasis(geo::Vec3{0.0, 0.0, 1.0});
    EXPECT_NEAR(basis.uAxis.x, 0.0, 1e-9);
    EXPECT_NEAR(basis.uAxis.y, 1.0, 1e-9);
    EXPECT_NEAR(basis.uAxis.z, 0.0, 1e-9);
    EXPECT_NEAR(basis.vAxis.x, -1.0, 1e-9);
    EXPECT_NEAR(basis.vAxis.y, 0.0, 1e-9);
    EXPECT_NEAR(basis.vAxis.z, 0.0, 1e-9);
    // Still orthonormal, same invariant as the non-fallback case.
    EXPECT_NEAR(geo::length(basis.uAxis), 1.0, 1e-9);
    EXPECT_NEAR(geo::dot(basis.uAxis, basis.vAxis), 0.0, 1e-9);
}

TEST(MaterialBatch, FaceUvBasis_NegativeVerticalNormalFallsBackToWorldXHelper) {
    // The downward-facing counterpart -- same fallback, mirrored basis.
    const FaceUvBasis basis = faceUvBasis(geo::Vec3{0.0, 0.0, -1.0});
    EXPECT_NEAR(basis.uAxis.x, 0.0, 1e-9);
    EXPECT_NEAR(basis.uAxis.y, -1.0, 1e-9);
    EXPECT_NEAR(basis.uAxis.z, 0.0, 1e-9);
    EXPECT_NEAR(basis.vAxis.x, -1.0, 1e-9);
    EXPECT_NEAR(basis.vAxis.y, 0.0, 1e-9);
    EXPECT_NEAR(basis.vAxis.z, 0.0, 1e-9);
}

TEST(MaterialBatch, FaceUvBasis_DegenerateNormalIsSafeZero) {
    // Zero-length input normal (shouldn't happen for a valid Face, but
    // defensive, same stance geo::normalized documents) must not divide by
    // zero or NaN: both axes collapse to zero, faceVertexUv reads {0,0}.
    const FaceUvBasis basis = faceUvBasis(geo::Vec3{0.0, 0.0, 0.0});
    EXPECT_DOUBLE_EQ(basis.uAxis.x, 0.0);
    EXPECT_DOUBLE_EQ(basis.uAxis.y, 0.0);
    EXPECT_DOUBLE_EQ(basis.uAxis.z, 0.0);
    EXPECT_DOUBLE_EQ(basis.vAxis.x, 0.0);
    EXPECT_DOUBLE_EQ(basis.vAxis.y, 0.0);
    EXPECT_DOUBLE_EQ(basis.vAxis.z, 0.0);

    const Uv uv = faceVertexUv(basis, geo::Vec3{5.0, -3.0, 7.0}, 1.0, 1.0);
    EXPECT_DOUBLE_EQ(uv.u, 0.0);
    EXPECT_DOUBLE_EQ(uv.v, 0.0);
}

TEST(MaterialBatch, FaceVertexUv_TilingScalesInversely) {
    // normal = worldY -- helper stays worldZ: uAxis = worldX, vAxis =
    // -worldZ, so a point displaced purely along worldX exercises u's
    // tileW scaling cleanly without v entering the picture.
    const FaceUvBasis basis = faceUvBasis(geo::Vec3{0.0, 1.0, 0.0});
    const geo::Vec3 p{4.0, 0.0, 0.0};

    const Uv uvTile1 = faceVertexUv(basis, p, /*tileW=*/1.0, /*tileH=*/1.0);
    EXPECT_NEAR(uvTile1.u, 4.0, 1e-9);
    EXPECT_NEAR(uvTile1.v, 0.0, 1e-9);

    // Doubling tileW halves u -- the pattern now repeats every 2 model units
    // instead of every 1.
    const Uv uvTile2 = faceVertexUv(basis, p, /*tileW=*/2.0, /*tileH=*/1.0);
    EXPECT_NEAR(uvTile2.u, 2.0, 1e-9);
}

TEST(MaterialBatch, FaceVertexUv_SlantedFaceMatchesExpectedProjection) {
    // 45-degree slanted normal (1,0,1)/sqrt2 -- neither axis-aligned nor
    // vertical, exercises the ordinary worldZ-helper path with a
    // non-axis-aligned basis, checked against a hand-computed expected UV.
    const geo::Vec3 normal{1.0, 0.0, 1.0};
    const FaceUvBasis basis = faceUvBasis(normal);
    // Expected basis (hand-derived): uAxis = normalize(cross(n, worldZ)) =
    // (0,-1,0); vAxis = cross(n, uAxis) = (1/sqrt2, 0, -1/sqrt2).
    EXPECT_NEAR(basis.uAxis.x, 0.0, 1e-9);
    EXPECT_NEAR(basis.uAxis.y, -1.0, 1e-9);
    EXPECT_NEAR(basis.uAxis.z, 0.0, 1e-9);
    const double invSqrt2 = 1.0 / std::sqrt(2.0);
    EXPECT_NEAR(basis.vAxis.x, invSqrt2, 1e-9);
    EXPECT_NEAR(basis.vAxis.y, 0.0, 1e-9);
    EXPECT_NEAR(basis.vAxis.z, -invSqrt2, 1e-9);

    const Uv uv = faceVertexUv(basis, geo::Vec3{1.0, 2.0, -1.0}, /*tileW=*/1.0, /*tileH=*/1.0);
    EXPECT_NEAR(uv.u, -2.0, 1e-9);             // dot((1,2,-1), (0,-1,0))
    EXPECT_NEAR(uv.v, std::sqrt(2.0), 1e-9);  // dot((1,2,-1), (1/sqrt2,0,-1/sqrt2))
}

// ---- Per-face UV transform -- applyUvTransform's documented order:
// uv' = S(1/scaleU, 1/scaleV) * R(-rotationRad) * (uv - offset).

// Not <cmath>'s M_PI (MSVC hides it behind _USE_MATH_DEFINES) -- local
// constant, same convention as transform_entities_test.cpp's kHalfPi.
constexpr double kHalfPi = 1.5707963267948966;

TEST(MaterialBatch, ApplyUvTransform_IdentityIsNoOp) {
    const Uv uv{2.0, 3.0};
    const Uv result = applyUvTransform(uv, UvTransform{});  // {0,0,0,1,1} -- this struct's own default
    EXPECT_DOUBLE_EQ(result.u, 2.0);
    EXPECT_DOUBLE_EQ(result.v, 3.0);
}

TEST(MaterialBatch, ApplyUvTransform_PureOffsetSubtractsOffset) {
    const Uv uv{5.0, 5.0};
    const UvTransform t{/*offsetU=*/1.0, /*offsetV=*/-2.0, /*rotationRad=*/0.0, /*scaleU=*/1.0, /*scaleV=*/1.0};
    const Uv result = applyUvTransform(uv, t);
    EXPECT_NEAR(result.u, 4.0, 1e-9);  // 5 - 1
    EXPECT_NEAR(result.v, 7.0, 1e-9);  // 5 - (-2)
}

TEST(MaterialBatch, ApplyUvTransform_NinetyDegreeRotationHandCase) {
    // R(-90deg) applied to the +u axis vector (1,0): cos(-90)=0, sin(-90)=-1,
    // so ru = 1*0 - 0*(-1) = 0, rv = 1*(-1) + 0*0 = -1 -- (1,0) -> (0,-1).
    const UvTransform t{0.0, 0.0, /*rotationRad=*/kHalfPi, 1.0, 1.0};
    const Uv result1 = applyUvTransform(Uv{1.0, 0.0}, t);
    EXPECT_NEAR(result1.u, 0.0, 1e-9);
    EXPECT_NEAR(result1.v, -1.0, 1e-9);

    // Same rotation applied to the +v axis vector (0,1): ru = 0*0 - 1*(-1) =
    // 1, rv = 0*(-1) + 1*0 = 0 -- (0,1) -> (1,0).
    const Uv result2 = applyUvTransform(Uv{0.0, 1.0}, t);
    EXPECT_NEAR(result2.u, 1.0, 1e-9);
    EXPECT_NEAR(result2.v, 0.0, 1e-9);
}

TEST(MaterialBatch, ApplyUvTransform_ScaleDividesByScale) {
    const Uv uv{6.0, 8.0};
    const UvTransform t{0.0, 0.0, 0.0, /*scaleU=*/2.0, /*scaleV=*/4.0};
    const Uv result = applyUvTransform(uv, t);
    EXPECT_NEAR(result.u, 3.0, 1e-9);  // 6 / 2
    EXPECT_NEAR(result.v, 2.0, 1e-9);  // 8 / 4
}

TEST(MaterialBatch, ApplyUvTransform_ComposedCaseTranslateThenRotateThenScale) {
    // uv=(2,1), offset=(1,1) -> (1,0); R(-90) of (1,0) = (0,-1) (same hand
    // case as the rotation test above); /scale(2,2) -> (0,-0.5).
    const Uv uv{2.0, 1.0};
    const UvTransform t{/*offsetU=*/1.0, /*offsetV=*/1.0, /*rotationRad=*/kHalfPi, /*scaleU=*/2.0, /*scaleV=*/2.0};
    const Uv result = applyUvTransform(uv, t);
    EXPECT_NEAR(result.u, 0.0, 1e-9);
    EXPECT_NEAR(result.v, -0.5, 1e-9);
}

}  // namespace
}  // namespace plnr::viewport
