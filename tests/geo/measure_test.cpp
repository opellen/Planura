#include <geo/measure.h>

#include <gtest/gtest.h>

namespace {

using plnr::geo::edgeLength;
using plnr::geo::faceArea;
using plnr::geo::Id;
using plnr::geo::kInvalidId;
using plnr::geo::Model;
using plnr::geo::Vec3;

TEST(MeasureTest, EdgeLengthOfKnownEdgeMatchesEndpointDistance) {
    Model model;
    const Vec3 p0{0.0, 0.0, 0.0};
    const Vec3 p1{3.0, 4.0, 0.0};  // 3-4-5 triangle -- exact length 5.0
    const Id edgeId = model.addEdge(p0, p1).edge;
    ASSERT_NE(edgeId, kInvalidId);

    EXPECT_DOUBLE_EQ(edgeLength(model, edgeId), 5.0);
}

TEST(MeasureTest, EdgeLengthOfUnknownIdIsZero) {
    Model model;
    EXPECT_DOUBLE_EQ(edgeLength(model, 999999), 0.0);
}

TEST(MeasureTest, FaceAreaOfFourByThreeRectangleIsTwelve) {
    Model model;
    const Vec3 p0{0.0, 0.0, 0.0};
    const Vec3 p1{4.0, 0.0, 0.0};
    const Vec3 p2{4.0, 3.0, 0.0};
    const Vec3 p3{0.0, 3.0, 0.0};
    ASSERT_TRUE(model.addEdge(p0, p1).created);
    ASSERT_TRUE(model.addEdge(p1, p2).created);
    ASSERT_TRUE(model.addEdge(p2, p3).created);
    const auto closing = model.addEdge(p3, p0);
    ASSERT_TRUE(closing.created);
    ASSERT_EQ(closing.newFaces.size(), 1u);

    EXPECT_NEAR(faceArea(model, closing.newFaces[0]), 12.0, 1e-9);
}

TEST(MeasureTest, FaceAreaOfUnknownIdIsZero) {
    Model model;
    EXPECT_DOUBLE_EQ(faceArea(model, 999999), 0.0);
}

}  // namespace
