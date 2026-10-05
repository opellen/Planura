#include <geo/offset.h>

#include <gtest/gtest.h>

namespace {

using plnr::geo::kMergeTol;
using plnr::geo::OffsetResult;
using plnr::geo::offsetLoop;
using plnr::geo::Vec3;

void expectPointNear(const Vec3& actual, const Vec3& expected, double tol = 1e-9) {
    EXPECT_NEAR(actual.x, expected.x, tol);
    EXPECT_NEAR(actual.y, expected.y, tol);
    EXPECT_NEAR(actual.z, expected.z, tol);
}

// A unit-square loop, CCW as seen looking against +Z (positive shoelace
// area) -- the fixture every closed-loop test below offsets.
const std::vector<Vec3> kUnitSquareCcw = {
    {0.0, 0.0, 0.0},
    {1.0, 0.0, 0.0},
    {1.0, 1.0, 0.0},
    {0.0, 1.0, 0.0},
};

// --- Sign convention -------------------------------------------------------

TEST(OffsetTest, SignConventionSingleOpenSegmentShiftsAlongCrossPlaneNormalSegDir) {
    // Simplest possible case: one open segment along +X, plane normal +Z.
    // shiftDir = normalize(cross(planeNormal, segDir)) = cross((0,0,1),(1,0,0))
    // = (0,1,0) -- documented in offset.h's own sign-convention paragraph.
    const std::vector<Vec3> loop = {{0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}};

    const OffsetResult result = offsetLoop(loop, Vec3{0.0, 0.0, 1.0}, 1.0, /*closed=*/false, /*keepOverlaps=*/false);

    ASSERT_TRUE(result.ok);
    ASSERT_EQ(result.points.size(), 2u);
    expectPointNear(result.points[0], Vec3{0.0, 1.0, 0.0});
    expectPointNear(result.points[1], Vec3{1.0, 1.0, 0.0});
}

TEST(OffsetTest, SquareOffsetInwardWithPositiveDistanceKnownCoordinates) {
    // CCW loop, positive distance -- offset.h documents this as INWARD.
    const OffsetResult result =
        offsetLoop(kUnitSquareCcw, Vec3{0.0, 0.0, 1.0}, 0.2, /*closed=*/true, /*keepOverlaps=*/false);

    ASSERT_TRUE(result.ok);
    ASSERT_EQ(result.points.size(), 4u);
    expectPointNear(result.points[0], Vec3{0.2, 0.2, 0.0});
    expectPointNear(result.points[1], Vec3{0.8, 0.2, 0.0});
    expectPointNear(result.points[2], Vec3{0.8, 0.8, 0.0});
    expectPointNear(result.points[3], Vec3{0.2, 0.8, 0.0});
}

TEST(OffsetTest, SquareOffsetOutwardWithNegativeDistanceKnownCoordinates) {
    // Same CCW loop, negative distance -- the other side (outward).
    const OffsetResult result =
        offsetLoop(kUnitSquareCcw, Vec3{0.0, 0.0, 1.0}, -0.2, /*closed=*/true, /*keepOverlaps=*/false);

    ASSERT_TRUE(result.ok);
    ASSERT_EQ(result.points.size(), 4u);
    expectPointNear(result.points[0], Vec3{-0.2, -0.2, 0.0});
    expectPointNear(result.points[1], Vec3{1.2, -0.2, 0.0});
    expectPointNear(result.points[2], Vec3{1.2, 1.2, 0.0});
    expectPointNear(result.points[3], Vec3{-0.2, 1.2, 0.0});
}

// --- Joins -------------------------------------------------------------

TEST(OffsetTest, NearParallelJoinFallsBackToSharedTranslatedPoint) {
    // Three colinear points -- the two segments meeting at the middle one are
    // exactly parallel (cross == 0), so the join falls back to the shared-
    // translated-point rule instead of an ill-conditioned line intersection.
    const std::vector<Vec3> loop = {{0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, {2.0, 0.0, 0.0}};

    const OffsetResult result = offsetLoop(loop, Vec3{0.0, 0.0, 1.0}, 0.3, /*closed=*/false, /*keepOverlaps=*/false);

    ASSERT_TRUE(result.ok);
    ASSERT_EQ(result.points.size(), 3u);
    expectPointNear(result.points[0], Vec3{0.0, 0.3, 0.0});
    expectPointNear(result.points[1], Vec3{1.0, 0.3, 0.0});
    expectPointNear(result.points[2], Vec3{2.0, 0.3, 0.0});
}

TEST(OffsetTest, OpenLChainOffsetBothSidesOfTheBend) {
    // An open two-segment L: (0,0)->(2,0)->(2,2). Offsetting to each side
    // (opposite-sign distance) exercises the open-chain endpoint-translate
    // rule (no join at either end of the chain).
    const std::vector<Vec3> loop = {{0.0, 0.0, 0.0}, {2.0, 0.0, 0.0}, {2.0, 2.0, 0.0}};

    const OffsetResult positive =
        offsetLoop(loop, Vec3{0.0, 0.0, 1.0}, 0.5, /*closed=*/false, /*keepOverlaps=*/false);
    ASSERT_TRUE(positive.ok);
    ASSERT_EQ(positive.points.size(), 3u);
    expectPointNear(positive.points[0], Vec3{0.0, 0.5, 0.0});
    expectPointNear(positive.points[1], Vec3{1.5, 0.5, 0.0});
    expectPointNear(positive.points[2], Vec3{1.5, 2.0, 0.0});

    const OffsetResult negative =
        offsetLoop(loop, Vec3{0.0, 0.0, 1.0}, -0.5, /*closed=*/false, /*keepOverlaps=*/false);
    ASSERT_TRUE(negative.ok);
    ASSERT_EQ(negative.points.size(), 3u);
    expectPointNear(negative.points[0], Vec3{0.0, -0.5, 0.0});
    expectPointNear(negative.points[1], Vec3{2.5, -0.5, 0.0});
    expectPointNear(negative.points[2], Vec3{2.5, 2.0, 0.0});
}

// --- Overlap cleanup -----------------------------------------------------

TEST(OffsetTest, TriangleInwardOffsetPastIncenterCollapses) {
    // A small CCW triangle (inradius ~0.414) offset far past its incenter --
    // every edge inverts, so even the naive cleanup can't recover a valid
    // (>= 3 point) result: ok=false.
    const std::vector<Vec3> triangle = {{0.0, 0.0, 0.0}, {2.0, 0.0, 0.0}, {1.0, 1.0, 0.0}};

    const OffsetResult result =
        offsetLoop(triangle, Vec3{0.0, 0.0, 1.0}, 5.0, /*closed=*/true, /*keepOverlaps=*/false);

    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(result.points.empty());
}

TEST(OffsetTest, KeepOverlapsDiffersFromCleanedOnConcaveHexagonInwardOffset) {
    // An L-shaped (concave) hexagon, CCW. At distance 0.6 the two short
    // edges bordering the reflex notch (p1->p2 and p4->p5) invert -- hand-
    // verified via the miter-join construction offset.h documents.
    const std::vector<Vec3> lShape = {
        {0.0, 0.0, 0.0}, {2.0, 0.0, 0.0}, {2.0, 1.0, 0.0}, {1.0, 1.0, 0.0}, {1.0, 2.0, 0.0}, {0.0, 2.0, 0.0},
    };
    const Vec3 normal{0.0, 0.0, 1.0};
    const double distance = 0.6;

    const OffsetResult withOverlaps = offsetLoop(lShape, normal, distance, /*closed=*/true, /*keepOverlaps=*/true);
    const OffsetResult cleaned = offsetLoop(lShape, normal, distance, /*closed=*/true, /*keepOverlaps=*/false);

    ASSERT_TRUE(withOverlaps.ok);
    ASSERT_TRUE(cleaned.ok);

    // keepOverlaps=true keeps every miter join, including the two inverted
    // (self-overlapping) segments -- one point per source vertex.
    ASSERT_EQ(withOverlaps.points.size(), 6u);
    expectPointNear(withOverlaps.points[0], Vec3{0.6, 0.6, 0.0});
    expectPointNear(withOverlaps.points[1], Vec3{1.4, 0.6, 0.0});
    expectPointNear(withOverlaps.points[2], Vec3{1.4, 0.4, 0.0});
    expectPointNear(withOverlaps.points[3], Vec3{0.4, 0.4, 0.0});
    expectPointNear(withOverlaps.points[4], Vec3{0.4, 1.4, 0.0});
    expectPointNear(withOverlaps.points[5], Vec3{0.6, 1.4, 0.0});

    // Cleaned drops the two inverted segments and rejoins their surviving
    // neighbors directly, collapsing the notch region to two new points.
    ASSERT_EQ(cleaned.points.size(), 4u);
    expectPointNear(cleaned.points[0], Vec3{0.6, 0.6, 0.0});
    expectPointNear(cleaned.points[1], Vec3{2.0, 0.5, 0.0});
    expectPointNear(cleaned.points[2], Vec3{0.4, 0.4, 0.0});
    expectPointNear(cleaned.points[3], Vec3{0.5, 2.0, 0.0});
}

// --- Guards ----------------------------------------------------------------

TEST(OffsetTest, GuardsReturnNotOk) {
    const Vec3 n{0.0, 0.0, 1.0};

    // Open chain needs >= 2 points.
    EXPECT_FALSE(offsetLoop({{0.0, 0.0, 0.0}}, n, 1.0, /*closed=*/false, false).ok);
    // Closed loop needs >= 3 points.
    EXPECT_FALSE(offsetLoop({{0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}}, n, 1.0, /*closed=*/true, false).ok);
    // Distance below merge tolerance.
    EXPECT_FALSE(offsetLoop(kUnitSquareCcw, n, kMergeTol * 0.5, /*closed=*/true, false).ok);
    // Zero-length source segment (first two points coincide).
    EXPECT_FALSE(
        offsetLoop({{0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}}, n, 1.0, /*closed=*/false, false).ok);
    // Degenerate (zero) plane normal.
    EXPECT_FALSE(offsetLoop(kUnitSquareCcw, Vec3{0.0, 0.0, 0.0}, 0.2, /*closed=*/true, false).ok);
}

}  // namespace
