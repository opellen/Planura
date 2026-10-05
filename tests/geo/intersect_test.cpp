#include <geo/intersect.h>

#include <gtest/gtest.h>

namespace {

using plnr::geo::kMergeTol;
using plnr::geo::segmentFacePlaneIntersect;
using plnr::geo::segmentIntersect;
using plnr::geo::Vec3;

void expectPointNear(const Vec3& actual, const Vec3& expected, double tol = 1e-9) {
    EXPECT_NEAR(actual.x, expected.x, tol);
    EXPECT_NEAR(actual.y, expected.y, tol);
    EXPECT_NEAR(actual.z, expected.z, tol);
}

// --- segmentIntersect --------------------------------------------------

TEST(IntersectTest, CrossingSegmentsHitAtExpectedPoint) {
    // Two segments in the z=0 plane crossing at their shared midpoint.
    const auto hit = segmentIntersect(Vec3{0.0, 0.0, 0.0}, Vec3{1.0, 1.0, 0.0}, Vec3{0.0, 1.0, 0.0},
                                       Vec3{1.0, 0.0, 0.0}, kMergeTol);

    ASSERT_TRUE(hit.has_value());
    expectPointNear(*hit, Vec3{0.5, 0.5, 0.0});
}

TEST(IntersectTest, TTouchAtSharedEndpointIsNullopt) {
    // B's own endpoint (1,0,0) lands exactly at A's midpoint -- a "T", not a
    // proper crossing: interior to A, but AT b1's own endpoint on B.
    const auto hit = segmentIntersect(Vec3{0.0, 0.0, 0.0}, Vec3{2.0, 0.0, 0.0}, Vec3{1.0, 0.0, 0.0},
                                       Vec3{1.0, 1.0, 0.0}, kMergeTol);

    EXPECT_FALSE(hit.has_value());
}

TEST(IntersectTest, ParallelSegmentsAreNullopt) {
    const auto hit = segmentIntersect(Vec3{0.0, 0.0, 0.0}, Vec3{1.0, 0.0, 0.0}, Vec3{0.0, 1.0, 0.0},
                                       Vec3{1.0, 1.0, 0.0}, kMergeTol);

    EXPECT_FALSE(hit.has_value());
}

TEST(IntersectTest, CollinearOverlapIsNullopt) {
    // Same infinite line, overlapping spans -- no single crossing point.
    const auto hit = segmentIntersect(Vec3{0.0, 0.0, 0.0}, Vec3{2.0, 0.0, 0.0}, Vec3{1.0, 0.0, 0.0},
                                       Vec3{3.0, 0.0, 0.0}, kMergeTol);

    EXPECT_FALSE(hit.has_value());
}

TEST(IntersectTest, NonCoplanarSkewSegmentsAreNullopt) {
    // Line A along +X at z=0; line B along +Y at z=1, directly above A's
    // midpoint -- closest points are 1 unit apart in z, far beyond kMergeTol.
    const auto hit = segmentIntersect(Vec3{0.0, 0.0, 0.0}, Vec3{1.0, 0.0, 0.0}, Vec3{0.5, -1.0, 1.0},
                                       Vec3{0.5, 1.0, 1.0}, kMergeTol);

    EXPECT_FALSE(hit.has_value());
}

TEST(IntersectTest, NearCoplanarWithinTolStillHits) {
    // Same layout as the skew case above, but the z-offset (5e-5) is inside
    // kMergeTol (1e-4) -- must still resolve to a crossing point, the
    // midpoint of the two lines' closest points.
    const double zOffset = 5e-5;
    const auto hit = segmentIntersect(Vec3{0.0, 0.0, 0.0}, Vec3{1.0, 0.0, 0.0}, Vec3{0.5, -1.0, zOffset},
                                       Vec3{0.5, 1.0, zOffset}, kMergeTol);

    ASSERT_TRUE(hit.has_value());
    expectPointNear(*hit, Vec3{0.5, 0.0, zOffset * 0.5});
}

// --- segmentFacePlaneIntersect ------------------------------------------

TEST(IntersectTest, SegmentPlaneBasicCrossing) {
    const auto hit = segmentFacePlaneIntersect(Vec3{0.0, 0.0, -1.0}, Vec3{0.0, 0.0, 1.0}, Vec3{0.0, 0.0, 0.0},
                                                Vec3{0.0, 0.0, 1.0}, kMergeTol);

    ASSERT_TRUE(hit.has_value());
    expectPointNear(*hit, Vec3{0.0, 0.0, 0.0});
}

TEST(IntersectTest, SegmentParallelToPlaneIsNullopt) {
    // Segment direction (+X) is perpendicular to the plane normal (+Z) --
    // parallel to the plane, no unique crossing (regardless of the z offset
    // between the segment and the plane itself).
    const auto hit = segmentFacePlaneIntersect(Vec3{0.0, 0.0, 1.0}, Vec3{1.0, 0.0, 1.0}, Vec3{0.0, 0.0, 0.0},
                                                Vec3{0.0, 0.0, 1.0}, kMergeTol);

    EXPECT_FALSE(hit.has_value());
}

TEST(IntersectTest, SegmentPlaneCrossingOutsideSegmentSpanIsNullopt) {
    // The plane z=0 is well beyond both endpoints of a segment sitting
    // entirely at z in [1, 2].
    const auto hit = segmentFacePlaneIntersect(Vec3{0.0, 0.0, 1.0}, Vec3{0.0, 0.0, 2.0}, Vec3{0.0, 0.0, 0.0},
                                                Vec3{0.0, 0.0, 1.0}, kMergeTol);

    EXPECT_FALSE(hit.has_value());
}

}  // namespace
