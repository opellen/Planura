#include <geo/scene.h>

#include <cmath>
#include <numbers>

#include <gtest/gtest.h>

namespace {

using plnr::geo::almostEqual;
using plnr::geo::distance;
using plnr::geo::Transform;
using plnr::geo::Vec3;

constexpr double kPi = std::numbers::pi;

// -- rotation --------------------------------------------------------------

TEST(RotationTest, NinetyDegreesAboutZAtOriginMapsXAxisToYAxis) {
    const Transform r = Transform::rotation(Vec3{0.0, 0.0, 0.0}, Vec3{0.0, 0.0, 1.0}, kPi / 2.0);
    EXPECT_TRUE(almostEqual(r.apply(Vec3{1.0, 0.0, 0.0}), Vec3{0.0, 1.0, 0.0}, 1e-9));
}

TEST(RotationTest, OffOriginPivotPointStaysFixed) {
    const Vec3 pivot{3.0, -2.0, 1.0};
    const Transform r = Transform::rotation(pivot, Vec3{0.0, 0.0, 1.0}, kPi / 3.0);
    EXPECT_TRUE(almostEqual(r.apply(pivot), pivot, 1e-9));
}

TEST(RotationTest, PreservesPairwiseDistances) {
    const Vec3 pivot{1.0, 1.0, 1.0};
    const Transform r = Transform::rotation(pivot, Vec3{1.0, 1.0, 0.0}, 0.7);
    const Vec3 a{4.0, 2.0, -3.0};
    const Vec3 b{-1.0, 5.0, 2.0};
    EXPECT_NEAR(distance(r.apply(a), r.apply(b)), distance(a, b), 1e-9);
}

// -- scaling -----------------------------------------------------------------

TEST(ScalingTest, CenterStaysFixed) {
    const Vec3 center{2.0, -1.0, 5.0};
    const Transform s = Transform::scaling(center, 2.0, 3.0, 0.5);
    EXPECT_TRUE(almostEqual(s.apply(center), center, 1e-9));
}

TEST(ScalingTest, PerAxisFactorsApplyRelativeToCenter) {
    const Vec3 center{1.0, 1.0, 1.0};
    const Transform s = Transform::scaling(center, 2.0, 3.0, 4.0);
    // center + {1,1,1} scales to center + {2,3,4} per axis.
    const Vec3 p = center + Vec3{1.0, 1.0, 1.0};
    EXPECT_TRUE(almostEqual(s.apply(p), center + Vec3{2.0, 3.0, 4.0}, 1e-9));
}

TEST(ScalingTest, UniformScaleScalesDistancesByTheFactor) {
    const Vec3 center{0.0, 0.0, 0.0};
    const Transform s = Transform::scaling(center, 2.0, 2.0, 2.0);
    const Vec3 a{1.0, 0.0, 0.0};
    const Vec3 b{0.0, 1.0, 0.0};
    EXPECT_NEAR(distance(s.apply(a), s.apply(b)), distance(a, b) * 2.0, 1e-9);
}

// -- mirror --------------------------------------------------------------

TEST(MirrorTest, PlanePointsStayFixed) {
    const Vec3 planePoint{0.0, 0.0, 2.0};
    const Transform m = Transform::mirror(planePoint, Vec3{0.0, 0.0, 1.0});
    // Any point in the z=2 plane, not just planePoint itself, is fixed.
    const Vec3 inPlane{5.0, -3.0, 2.0};
    EXPECT_TRUE(almostEqual(m.apply(planePoint), planePoint, 1e-9));
    EXPECT_TRUE(almostEqual(m.apply(inPlane), inPlane, 1e-9));
}

TEST(MirrorTest, ApplyingTwiceIsIdentity) {
    const Transform m = Transform::mirror(Vec3{1.0, 2.0, 3.0}, Vec3{1.0, 1.0, 1.0});
    const Vec3 p{10.0, -4.0, 7.0};
    EXPECT_TRUE(almostEqual(m.apply(m.apply(p)), p, 1e-9));
}

TEST(MirrorTest, ReversesTheNormalComponentOfAVector) {
    const Vec3 n{0.0, 0.0, 1.0};
    const Transform m = Transform::mirror(Vec3{0.0, 0.0, 0.0}, n);
    // A vector purely along the normal direction flips sign under the linear
    // part; translation is irrelevant to applyVector.
    EXPECT_TRUE(almostEqual(m.applyVector(n), -n, 1e-9));
}

// -- composed/inverse round-trips ------------------------------------------

TEST(TransformFactoryRoundTripTest, RotationInverseUndoesTheRotation) {
    const Transform r = Transform::rotation(Vec3{1.0, -1.0, 2.0}, Vec3{0.0, 1.0, 0.0}, 1.1);
    const Vec3 p{3.0, 4.0, -2.0};
    EXPECT_TRUE(almostEqual(r.inverse().apply(r.apply(p)), p, 1e-9));
    EXPECT_TRUE(r.composed(r.inverse()).almostEqual(Transform::identity(), 1e-9));
}

TEST(TransformFactoryRoundTripTest, ScalingInverseUndoesTheScale) {
    const Transform s = Transform::scaling(Vec3{2.0, 0.0, -1.0}, 2.0, 4.0, 0.5);
    const Vec3 p{5.0, -3.0, 1.0};
    EXPECT_TRUE(almostEqual(s.inverse().apply(s.apply(p)), p, 1e-9));
}

TEST(TransformFactoryRoundTripTest, MirrorIsItsOwnInverse) {
    const Transform m = Transform::mirror(Vec3{0.0, 0.0, 1.0}, Vec3{1.0, 0.0, 0.0});
    EXPECT_TRUE(m.inverse().almostEqual(m, 1e-9));
}

TEST(TransformFactoryRoundTripTest, ComposedRotationThenScalingMatchesStepByStep) {
    const Transform rot = Transform::rotation(Vec3{0.0, 0.0, 0.0}, Vec3{0.0, 0.0, 1.0}, kPi / 2.0);
    const Transform scale = Transform::scaling(Vec3{0.0, 0.0, 0.0}, 2.0, 2.0, 2.0);
    const Transform combined = rot.composed(scale);  // scale (inner) first, then rot (outer)
    const Vec3 p{1.0, 0.0, 0.0};
    EXPECT_TRUE(almostEqual(combined.apply(p), rot.apply(scale.apply(p)), 1e-9));
}

}  // namespace
