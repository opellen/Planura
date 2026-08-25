#include <geo/shapes.h>

#include <cmath>

#include <gtest/gtest.h>

namespace {

using plnr::geo::arcPoints2Point;
using plnr::geo::arcPoints3Point;
using plnr::geo::arcPointsCenter;
using plnr::geo::cross;
using plnr::geo::dot;
using plnr::geo::kArcDefaultSegments;
using plnr::geo::kMergeTol;
using plnr::geo::length;
using plnr::geo::normalized;
using plnr::geo::regularPolygonPoints;
using plnr::geo::Vec3;

constexpr double kPi = 3.14159265358979323846;

// Tighter equality check than the library's own almostEqual (which defaults
// to kMergeTol) -- these arc-construction checks are exact to floating-point
// precision, not just to merge tolerance.
bool almostEqualHelper(const Vec3& a, const Vec3& b) {
    return length(a - b) < 1e-9;
}

TEST(ShapesTest, HexagonAtOriginZNormalHasSixPointsAtRadiusFirstAlongStartDir) {
    const Vec3 center{0.0, 0.0, 0.0};
    const double radius = 2.0;

    const std::vector<Vec3> points = regularPolygonPoints(center, radius, Vec3{0, 0, 1}, 6, Vec3{1, 0, 0}, false);

    ASSERT_EQ(points.size(), 6u);
    for (const Vec3& p : points) {
        EXPECT_NEAR(length(p - center), radius, 1e-9);
    }
    // First vertex along startDir (+X).
    EXPECT_NEAR(points[0].x, radius, 1e-9);
    EXPECT_NEAR(points[0].y, 0.0, 1e-9);
    EXPECT_NEAR(points[0].z, 0.0, 1e-9);
}

TEST(ShapesTest, CircumscribedHexagonRadiusMatchesEdgeTangentFormula) {
    const double radius = 3.0;

    const std::vector<Vec3> points = regularPolygonPoints({0, 0, 0}, radius, Vec3{0, 0, 1}, 6, Vec3{1, 0, 0}, true);

    ASSERT_EQ(points.size(), 6u);
    const double expected = radius / std::cos(kPi / 6.0);
    for (const Vec3& p : points) {
        EXPECT_NEAR(length(p), expected, 1e-9);
    }
}

TEST(ShapesTest, ArbitraryNormalKeepsAllPointsInThePlaneAtTheRightRadius) {
    const Vec3 center{1.0, 2.0, 3.0};
    const Vec3 normal{1.0, 0.0, 0.0};  // +X, not the usual z-up ground plane
    const double radius = 1.5;

    const std::vector<Vec3> points = regularPolygonPoints(center, radius, normal, 8, Vec3{0, 1, 0}, false);

    ASSERT_EQ(points.size(), 8u);
    const Vec3 n = normalized(normal);
    for (const Vec3& p : points) {
        const Vec3 rel = p - center;
        EXPECT_NEAR(dot(rel, n), 0.0, 1e-9);
        EXPECT_NEAR(length(rel), radius, 1e-9);
    }
}

TEST(ShapesTest, GuardsReturnEmpty) {
    EXPECT_TRUE(regularPolygonPoints({0, 0, 0}, 1.0, Vec3{0, 0, 1}, 2, Vec3{1, 0, 0}, false).empty());  // segments < 3
    EXPECT_TRUE(
        regularPolygonPoints({0, 0, 0}, kMergeTol, Vec3{0, 0, 1}, 6, Vec3{1, 0, 0}, false).empty());  // radius too small
    EXPECT_TRUE(regularPolygonPoints({0, 0, 0}, 1.0, Vec3{0, 0, 0}, 6, Vec3{1, 0, 0}, false).empty());  // zero normal
}

TEST(ShapesTest, ArcDefaultSegmentsIsTwelve) {
    EXPECT_EQ(kArcDefaultSegments, 12);
}

// --- arcPoints2Point -----------------------------------------------------

TEST(ShapesTest, TwoPointArcHalfCircleAllPointsAtHalfChordRadiusFromMidpoint) {
    const Vec3 a{-1.0, 0.0, 0.0};
    const Vec3 b{1.0, 0.0, 0.0};
    const Vec3 normal{0.0, 0.0, 1.0};
    const Vec3 mid{0.0, 0.0, 0.0};
    const double halfChord = 1.0;  // bulge == c/2 -> exact half circle

    const std::vector<Vec3> points = arcPoints2Point(a, b, halfChord, normal, 8);

    ASSERT_EQ(points.size(), 9u);
    EXPECT_TRUE(almostEqualHelper(points.front(), a));
    EXPECT_TRUE(almostEqualHelper(points.back(), b));
    for (const Vec3& p : points) {
        EXPECT_NEAR(length(p - mid), halfChord, 1e-9);
    }
}

TEST(ShapesTest, TwoPointArcSignFlipsWhichSideOfTheChordItBulgesToward) {
    const Vec3 a{-1.0, 0.0, 0.0};
    const Vec3 b{1.0, 0.0, 0.0};
    const Vec3 normal{0.0, 0.0, 1.0};
    const Vec3 bulgeAxis = normalized(cross(normal, b - a));  // (0, 1, 0)

    const std::vector<Vec3> positive = arcPoints2Point(a, b, 1.0, normal, 8);
    const std::vector<Vec3> negative = arcPoints2Point(a, b, -1.0, normal, 8);

    ASSERT_EQ(positive.size(), 9u);
    ASSERT_EQ(negative.size(), 9u);
    // Midpoint of the arc (i == segments/2) sits on the bulge's declared side.
    EXPECT_GT(dot(positive[4] - Vec3{0, 0, 0}, bulgeAxis), 0.5);
    EXPECT_LT(dot(negative[4] - Vec3{0, 0, 0}, bulgeAxis), -0.5);
}

TEST(ShapesTest, TwoPointArcMajorArcRadiusMatchesSagittaFormula) {
    const Vec3 a{-1.0, 0.0, 0.0};
    const Vec3 b{1.0, 0.0, 0.0};
    const Vec3 normal{0.0, 0.0, 1.0};
    const double h = 1.0;
    const double s = 2.0;  // s > h -> major arc
    const double expectedRadius = (h * h + s * s) / (2.0 * s);
    const Vec3 bulgeDir = normalized(cross(normal, b - a));
    const Vec3 expectedCenter = Vec3{0, 0, 0} - bulgeDir * (expectedRadius - s);

    const std::vector<Vec3> points = arcPoints2Point(a, b, s, normal, 10);

    ASSERT_EQ(points.size(), 11u);
    EXPECT_TRUE(almostEqualHelper(points.front(), a));
    EXPECT_TRUE(almostEqualHelper(points.back(), b));
    for (const Vec3& p : points) {
        EXPECT_NEAR(length(p - expectedCenter), expectedRadius, 1e-9);
    }
}

TEST(ShapesTest, TwoPointArcStaysInThePlaneThroughAWithArbitraryNormal) {
    const Vec3 a{0.0, 0.0, 0.0};
    const Vec3 b{2.0, 0.0, 0.0};
    const Vec3 normal = normalized(Vec3{0.0, 1.0, 1.0});  // not perpendicular to chord in an axis-y way

    const std::vector<Vec3> points = arcPoints2Point(a, b, 0.5, normal, 8);

    ASSERT_EQ(points.size(), 9u);
    for (const Vec3& p : points) {
        EXPECT_NEAR(dot(p - a, normal), 0.0, 1e-9);
    }
}

TEST(ShapesTest, TwoPointArcGuardsReturnEmpty) {
    const Vec3 a{0.0, 0.0, 0.0};
    const Vec3 b{2.0, 0.0, 0.0};
    const Vec3 normal{0.0, 0.0, 1.0};
    EXPECT_TRUE(arcPoints2Point(a, a, 1.0, normal, 8).empty());               // no chord
    EXPECT_TRUE(arcPoints2Point(a, b, 0.0, normal, 8).empty());               // no sagitta
    EXPECT_TRUE(arcPoints2Point(a, b, 1.0, normal, 0).empty());               // segments < 1
    EXPECT_TRUE(arcPoints2Point(a, b, 1.0, Vec3{0, 0, 0}, 8).empty());        // zero normal
    EXPECT_TRUE(arcPoints2Point(a, b, 1.0, Vec3{1, 0, 0}, 8).empty());        // normal parallel to chord
}

// --- arcPointsCenter -------------------------------------------------------

TEST(ShapesTest, CenterArcQuarterSweepAboutZRotatesXTowardYWithCorrectMidpoint) {
    const Vec3 center{0.0, 0.0, 0.0};
    const Vec3 startPoint{2.0, 0.0, 0.0};
    const Vec3 normal{0.0, 0.0, 1.0};

    const std::vector<Vec3> points = arcPointsCenter(center, startPoint, kPi / 2.0, normal, 4);

    ASSERT_EQ(points.size(), 5u);
    EXPECT_TRUE(almostEqualHelper(points.front(), startPoint));
    EXPECT_TRUE(almostEqualHelper(points.back(), Vec3{0.0, 2.0, 0.0}));
    // Midpoint of the sweep (angle == pi/4).
    EXPECT_NEAR(points[2].x, 2.0 * std::cos(kPi / 4.0), 1e-9);
    EXPECT_NEAR(points[2].y, 2.0 * std::sin(kPi / 4.0), 1e-9);
    EXPECT_NEAR(points[2].z, 0.0, 1e-9);
}

TEST(ShapesTest, CenterArcNegativeSweepGoesTheOtherDirection) {
    const Vec3 center{0.0, 0.0, 0.0};
    const Vec3 startPoint{2.0, 0.0, 0.0};
    const Vec3 normal{0.0, 0.0, 1.0};

    const std::vector<Vec3> points = arcPointsCenter(center, startPoint, -kPi / 2.0, normal, 4);

    ASSERT_EQ(points.size(), 5u);
    EXPECT_TRUE(almostEqualHelper(points.front(), startPoint));
    EXPECT_TRUE(almostEqualHelper(points.back(), Vec3{0.0, -2.0, 0.0}));
}

TEST(ShapesTest, CenterArcFullSweepReturnsSegmentsPointsWithoutDuplicateEndpoint) {
    const Vec3 center{0.0, 0.0, 0.0};
    const Vec3 startPoint{2.0, 0.0, 0.0};
    const Vec3 normal{0.0, 0.0, 1.0};

    const std::vector<Vec3> points = arcPointsCenter(center, startPoint, 2.0 * kPi, normal, 6);

    ASSERT_EQ(points.size(), 6u);  // segments points, NOT segments + 1
    EXPECT_TRUE(almostEqualHelper(points.front(), startPoint));
    for (const Vec3& p : points) {
        EXPECT_NEAR(length(p - center), 2.0, 1e-9);
    }
}

TEST(ShapesTest, CenterArcGuardsReturnEmpty) {
    const Vec3 center{0.0, 0.0, 0.0};
    const Vec3 startPoint{2.0, 0.0, 0.0};
    const Vec3 normal{0.0, 0.0, 1.0};
    EXPECT_TRUE(arcPointsCenter(center, center, kPi / 2.0, normal, 4).empty());          // no radius
    EXPECT_TRUE(arcPointsCenter(center, startPoint, kPi / 2.0, Vec3{0, 0, 0}, 4).empty());  // zero normal
    EXPECT_TRUE(arcPointsCenter(center, startPoint, 0.0, normal, 4).empty());            // no sweep
    EXPECT_TRUE(arcPointsCenter(center, startPoint, 3.0 * kPi, normal, 4).empty());      // more than a full turn
    EXPECT_TRUE(arcPointsCenter(center, startPoint, kPi / 2.0, normal, 0).empty());      // segments < 1
}

// --- arcPoints3Point -------------------------------------------------------

TEST(ShapesTest, ThreePointArcKnownCircumcircleOrderedP1FirstP3Last) {
    const Vec3 p1{0.0, 0.0, 0.0};
    const Vec3 p2{1.0, 1.0, 0.0};
    const Vec3 p3{2.0, 0.0, 0.0};
    const Vec3 expectedCenter{1.0, 0.0, 0.0};
    const double expectedRadius = 1.0;

    const std::vector<Vec3> points = arcPoints3Point(p1, p2, p3, 8);

    ASSERT_EQ(points.size(), 9u);
    EXPECT_TRUE(almostEqualHelper(points.front(), p1));
    EXPECT_TRUE(almostEqualHelper(points.back(), p3));
    for (const Vec3& p : points) {
        EXPECT_NEAR(length(p - expectedCenter), expectedRadius, 1e-9);
        EXPECT_NEAR(p.z, 0.0, 1e-9);
    }
    // With 8 segments the sweep (pi, since p2 sits at the quarter-angle pi/2
    // of it) lands exactly on p2 at the midpoint sample.
    EXPECT_TRUE(almostEqualHelper(points[4], p2));
}

TEST(ShapesTest, ThreePointArcCollinearPointsReturnEmpty) {
    const Vec3 p1{0.0, 0.0, 0.0};
    const Vec3 p2{1.0, 0.0, 0.0};
    const Vec3 p3{2.0, 0.0, 0.0};
    EXPECT_TRUE(arcPoints3Point(p1, p2, p3, 8).empty());
}

TEST(ShapesTest, ThreePointArcGuardsReturnEmpty) {
    const Vec3 p1{0.0, 0.0, 0.0};
    const Vec3 p2{1.0, 1.0, 0.0};
    const Vec3 p3{2.0, 0.0, 0.0};
    EXPECT_TRUE(arcPoints3Point(p1, p1, p3, 8).empty());  // p1 == p2
    EXPECT_TRUE(arcPoints3Point(p1, p2, p3, 0).empty());  // segments < 1
}

}  // namespace
