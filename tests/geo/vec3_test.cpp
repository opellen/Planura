#include <geo/vec3.h>

#include <gtest/gtest.h>

namespace {

using plnr::geo::almostEqual;
using plnr::geo::cross;
using plnr::geo::distance;
using plnr::geo::dot;
using plnr::geo::kMergeTol;
using plnr::geo::length;
using plnr::geo::lengthSq;
using plnr::geo::normalized;
using plnr::geo::Vec3;

TEST(Vec3Test, AddSubNegate) {
    const Vec3 a{1.0, 2.0, 3.0};
    const Vec3 b{4.0, -1.0, 0.5};

    const Vec3 sum = a + b;
    EXPECT_DOUBLE_EQ(sum.x, 5.0);
    EXPECT_DOUBLE_EQ(sum.y, 1.0);
    EXPECT_DOUBLE_EQ(sum.z, 3.5);

    const Vec3 diff = a - b;
    EXPECT_DOUBLE_EQ(diff.x, -3.0);
    EXPECT_DOUBLE_EQ(diff.y, 3.0);
    EXPECT_DOUBLE_EQ(diff.z, 2.5);

    const Vec3 neg = -a;
    EXPECT_DOUBLE_EQ(neg.x, -1.0);
    EXPECT_DOUBLE_EQ(neg.y, -2.0);
    EXPECT_DOUBLE_EQ(neg.z, -3.0);
}

TEST(Vec3Test, ScalarMultiplyBothSides) {
    const Vec3 a{1.0, -2.0, 3.0};

    const Vec3 r1 = a * 2.0;
    const Vec3 r2 = 2.0 * a;

    EXPECT_DOUBLE_EQ(r1.x, 2.0);
    EXPECT_DOUBLE_EQ(r1.y, -4.0);
    EXPECT_DOUBLE_EQ(r1.z, 6.0);
    EXPECT_DOUBLE_EQ(r2.x, r1.x);
    EXPECT_DOUBLE_EQ(r2.y, r1.y);
    EXPECT_DOUBLE_EQ(r2.z, r1.z);
}

TEST(Vec3Test, DotProduct) {
    const Vec3 a{1.0, 2.0, 3.0};
    const Vec3 b{4.0, -5.0, 6.0};

    EXPECT_DOUBLE_EQ(dot(a, b), 1.0 * 4.0 + 2.0 * -5.0 + 3.0 * 6.0);
}

TEST(Vec3Test, CrossProductOfBasisVectors) {
    const Vec3 x{1.0, 0.0, 0.0};
    const Vec3 y{0.0, 1.0, 0.0};

    const Vec3 z = cross(x, y);

    EXPECT_DOUBLE_EQ(z.x, 0.0);
    EXPECT_DOUBLE_EQ(z.y, 0.0);
    EXPECT_DOUBLE_EQ(z.z, 1.0);
}

TEST(Vec3Test, LengthAndLengthSq) {
    const Vec3 a{3.0, 4.0, 0.0};

    EXPECT_DOUBLE_EQ(lengthSq(a), 25.0);
    EXPECT_DOUBLE_EQ(length(a), 5.0);
}

TEST(Vec3Test, NormalizedUnitLength) {
    const Vec3 a{3.0, 4.0, 0.0};

    const Vec3 n = normalized(a);

    EXPECT_NEAR(length(n), 1.0, 1e-12);
    EXPECT_NEAR(n.x, 0.6, 1e-12);
    EXPECT_NEAR(n.y, 0.8, 1e-12);
}

TEST(Vec3Test, NormalizedNearZeroReturnsZeroVector) {
    const Vec3 tiny{1e-12, 0.0, 0.0};

    const Vec3 n = normalized(tiny);

    EXPECT_DOUBLE_EQ(n.x, 0.0);
    EXPECT_DOUBLE_EQ(n.y, 0.0);
    EXPECT_DOUBLE_EQ(n.z, 0.0);
}

TEST(Vec3Test, DistanceBetweenPoints) {
    const Vec3 a{0.0, 0.0, 0.0};
    const Vec3 b{3.0, 4.0, 0.0};

    EXPECT_DOUBLE_EQ(distance(a, b), 5.0);
}

TEST(Vec3Test, AlmostEqualUsesDistanceAgainstTolerance) {
    const Vec3 a{0.0, 0.0, 0.0};
    const Vec3 within{kMergeTol * 0.5, 0.0, 0.0};
    const Vec3 outside{kMergeTol * 2.0, 0.0, 0.0};

    EXPECT_TRUE(almostEqual(a, within));
    EXPECT_FALSE(almostEqual(a, outside));
    EXPECT_TRUE(almostEqual(a, outside, kMergeTol * 3.0));
}

}  // namespace
