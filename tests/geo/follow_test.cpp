#include <geo/follow.h>

#include <array>
#include <cmath>
#include <vector>

#include <gtest/gtest.h>

namespace {

using plnr::geo::FollowResult;
using plnr::geo::sweep;
using plnr::geo::Vec3;

void expectPointNear(const Vec3& actual, const Vec3& expected, double tol = 1e-9) {
    EXPECT_NEAR(actual.x, expected.x, tol);
    EXPECT_NEAR(actual.y, expected.y, tol);
    EXPECT_NEAR(actual.z, expected.z, tol);
}

// A simple triangle profile in the XY plane -- the fixture most tests below sweep.
const std::vector<Vec3> kTriangleProfile = {
    {0.0, 0.0, 0.0},
    {1.0, 0.0, 0.0},
    {0.0, 1.0, 0.0},
};

// --- Straight (collinear) path: a pure prism ------------------------------

TEST(FollowTest, StraightTwoSegmentPathSectionsAreIdenticalUpToTranslation) {
    // Both segments run along +Z: collinear, cross(inDir, outDir) is zero, so no rotation fires.
    const std::vector<Vec3> path = {{0.0, 0.0, 0.0}, {0.0, 0.0, 1.0}, {0.0, 0.0, 2.0}};

    const FollowResult result = sweep(kTriangleProfile, path, /*closedPath=*/false);

    ASSERT_TRUE(result.ok);
    ASSERT_EQ(result.sections.size(), 3u);
    for (std::size_t k = 0; k < 3; ++k) {
        ASSERT_EQ(result.sections[k].size(), kTriangleProfile.size());
        for (std::size_t i = 0; i < kTriangleProfile.size(); ++i) {
            // Pure translation by path[k] - path[0], no rotation.
            expectPointNear(result.sections[k][i], kTriangleProfile[i] + (path[k] - path[0]));
        }
    }
}

TEST(FollowTest, StraightPathSideQuadsCountMatchesSegmentsTimesProfileSize) {
    const std::vector<Vec3> path = {{0.0, 0.0, 0.0}, {0.0, 0.0, 1.0}, {0.0, 0.0, 2.0}};

    const FollowResult result = sweep(kTriangleProfile, path, /*closedPath=*/false);

    ASSERT_TRUE(result.ok);
    // 2 segments (open path, 3 vertices) * 3 profile vertices = 6 quads.
    EXPECT_EQ(result.sideQuads.size(), 6u);
}

// --- L-shaped path: a 90-degree turn ---------------------------------------

TEST(FollowTest, LShapedPathSecondSectionRotated90DegreesKnownCoordinate) {
    // dir0=+X, dir1=+Y: vertex 1 is the only joint (vertex 2 has no outgoing segment, inherits its
    // rotation); axis=+Z, angle=90deg.
    const std::vector<Vec3> path = {{0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, {1.0, 1.0, 0.0}};

    const FollowResult result = sweep(kTriangleProfile, path, /*closedPath=*/false);

    ASSERT_TRUE(result.ok);
    ASSERT_EQ(result.sections.size(), 3u);

    // Section 0: identity rotation, zero translation -- exactly the profile.
    expectPointNear(result.sections[0][2], Vec3{0.0, 1.0, 0.0});

    // Section 1 (corner): (0,1,0) rotated 90deg about +Z = (-1,0,0), plus path[1] (1,0,0) = (0,0,0).
    expectPointNear(result.sections[1][2], Vec3{0.0, 0.0, 0.0});

    // Section 2 (last vertex): inherits section 1's rotation; (-1,0,0) + path[2] (1,1,0) = (0,1,0).
    expectPointNear(result.sections[2][2], Vec3{0.0, 1.0, 0.0});
}

// --- Closed square path: a lathe-style ring --------------------------------

TEST(FollowTest, ClosedSquarePathSectionCountMatchesPathAndClosingQuadsPresent) {
    const std::vector<Vec3> path = {
        {0.0, 0.0, 0.0},
        {1.0, 0.0, 0.0},
        {1.0, 1.0, 0.0},
        {0.0, 1.0, 0.0},
    };

    const FollowResult result = sweep(kTriangleProfile, path, /*closedPath=*/true);

    ASSERT_TRUE(result.ok);
    // No duplicated final section -- exactly one per path vertex.
    EXPECT_EQ(result.sections.size(), path.size());

    // 4 strips (3 interior + closing wrap) * 3 profile vertices = 12 quads; an open path gives only 3 strips.
    EXPECT_EQ(result.sideQuads.size(), path.size() * kTriangleProfile.size());

    // The wrap strip references flat indices in section 0 (0, 1, 2): stitched back, no duplicated section.
    bool foundWrapQuad = false;
    const int lastSectionBase = static_cast<int>((path.size() - 1) * kTriangleProfile.size());
    for (const std::array<int, 4>& quad : result.sideQuads) {
        if (quad[0] >= lastSectionBase && quad[3] < static_cast<int>(kTriangleProfile.size())) {
            foundWrapQuad = true;
            break;
        }
    }
    EXPECT_TRUE(foundWrapQuad);
}

// --- Lathe: circle path + perpendicular half-profile ----------------------

TEST(FollowTest, LatheCirclePathSectionCountMatchesPathAndClosingQuadsPresent) {
    // Follow Me lathe case: a 12-segment circle of radius 1 about +Z, and a half-profile in the XZ plane
    // (perpendicular to the path tangent at (1,0,0)) whose bottom corner sits at the circle's center.
    constexpr int kSegments = 12;
    constexpr double kPi = 3.14159265358979323846;
    std::vector<Vec3> path;
    for (int k = 0; k < kSegments; ++k) {
        const double a = 2.0 * kPi * k / kSegments;
        path.push_back({std::cos(a), std::sin(a), 0.0});
    }
    const std::vector<Vec3> profile = {
        {0.0, 0.0, 0.0},
        {0.9, 0.0, 0.0},
        {0.9, 0.0, 0.5},
        {0.0, 0.0, 0.5},
    };

    const FollowResult result = sweep(profile, path, /*closedPath=*/true);

    ASSERT_TRUE(result.ok);
    // One section per path vertex -- no duplicated final section on a closed path.
    ASSERT_EQ(result.sections.size(), path.size());
    // Every strip including the closing wrap: 12 strips * 4 profile vertices.
    EXPECT_EQ(result.sideQuads.size(), path.size() * profile.size());

    bool foundWrapQuad = false;
    const int lastSectionBase = static_cast<int>((path.size() - 1) * profile.size());
    for (const std::array<int, 4>& quad : result.sideQuads) {
        if (quad[0] >= lastSectionBase && quad[3] < static_cast<int>(profile.size())) {
            foundWrapQuad = true;
            break;
        }
    }
    EXPECT_TRUE(foundWrapQuad);

    // Reorientation: the path turns about +Z at every joint, so every section's top-edge height
    // is preserved across all 12 sections of the loop.
    for (const std::vector<Vec3>& section : result.sections) {
        ASSERT_EQ(section.size(), profile.size());
        EXPECT_NEAR(section[2].z, 0.5, 1e-9);
        EXPECT_NEAR(section[3].z, 0.5, 1e-9);
    }
}

// --- Guards ------------------------------------------------------------

TEST(FollowTest, ProfileWithFewerThanThreePointsRejected) {
    const std::vector<Vec3> profile = {{0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}};
    const std::vector<Vec3> path = {{0.0, 0.0, 0.0}, {0.0, 0.0, 1.0}};

    const FollowResult result = sweep(profile, path, /*closedPath=*/false);

    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(result.sections.empty());
    EXPECT_TRUE(result.sideQuads.empty());
}

TEST(FollowTest, PathWithFewerThanTwoPointsRejected) {
    const std::vector<Vec3> path = {{0.0, 0.0, 0.0}};

    const FollowResult result = sweep(kTriangleProfile, path, /*closedPath=*/false);

    EXPECT_FALSE(result.ok);
}

TEST(FollowTest, ZeroLengthPathSegmentRejected) {
    const std::vector<Vec3> path = {{0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, {0.0, 0.0, 1.0}};

    const FollowResult result = sweep(kTriangleProfile, path, /*closedPath=*/false);

    EXPECT_FALSE(result.ok);
}

TEST(FollowTest, ZeroLengthWrapSegmentRejectedWhenClosed) {
    // Open segments are fine, but the closing wrap (last -> first) is zero-length.
    const std::vector<Vec3> path = {{0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, {0.0, 0.0, 0.0}};

    const FollowResult result = sweep(kTriangleProfile, path, /*closedPath=*/true);

    EXPECT_FALSE(result.ok);
}

TEST(FollowTest, DegenerateCollinearProfileRejected) {
    const std::vector<Vec3> profile = {{0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, {2.0, 0.0, 0.0}};
    const std::vector<Vec3> path = {{0.0, 0.0, 0.0}, {0.0, 0.0, 1.0}};

    const FollowResult result = sweep(profile, path, /*closedPath=*/false);

    EXPECT_FALSE(result.ok);
}

}  // namespace
