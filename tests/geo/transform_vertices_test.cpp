#include <geo/model.h>
#include <geo/scene.h>

#include <numbers>
#include <vector>

#include <gtest/gtest.h>

namespace {

using plnr::geo::almostEqual;
using plnr::geo::Id;
using plnr::geo::Model;
using plnr::geo::Transform;
using plnr::geo::Vec3;

constexpr double kPi = std::numbers::pi;

// Ground-plane triangle (0,0,0)-(1,0,0)-(0,1,0).
Id buildGroundTriangle(Model& model) {
    const Vec3 p0{0.0, 0.0, 0.0};
    const Vec3 p1{1.0, 0.0, 0.0};
    const Vec3 p2{0.0, 1.0, 0.0};

    model.addEdge(p0, p1);
    model.addEdge(p1, p2);
    const auto closing = model.addEdge(p2, p0);
    return closing.newFaces.empty() ? plnr::geo::kInvalidId : closing.newFaces[0];
}

TEST(TransformVerticesTest, HappyPathMovesVerticesAndUpdatesFaceNormal) {
    Model model;
    const Id faceId = buildGroundTriangle(model);
    ASSERT_NE(faceId, plnr::geo::kInvalidId);

    const std::vector<Id> loop = model.faceVertexLoop(faceId);
    ASSERT_EQ(loop.size(), 3u);

    std::vector<Vec3> before;
    for (Id v : loop) {
        before.push_back(model.vertex(v)->pos);
    }
    const Vec3 normalBefore = model.face(faceId)->normal;

    const Transform xf = Transform::rotation(Vec3{0.0, 0.0, 0.0}, Vec3{1.0, 0.0, 0.0}, kPi / 2.0);
    ASSERT_TRUE(model.transformVertices(loop, xf));

    for (std::size_t i = 0; i < loop.size(); ++i) {
        EXPECT_TRUE(almostEqual(model.vertex(loop[i])->pos, xf.apply(before[i]), 1e-9));
    }

    const Vec3 expectedNormal = xf.applyVector(normalBefore);
    EXPECT_TRUE(almostEqual(model.face(faceId)->normal, expectedNormal, 1e-9));
}

TEST(TransformVerticesTest, UnknownVertexIdIsRejectedWithoutMutation) {
    Model model;
    model.addEdge(Vec3{0.0, 0.0, 0.0}, Vec3{1.0, 0.0, 0.0});
    const auto* v0 = model.findVertex(Vec3{0.0, 0.0, 0.0});
    ASSERT_NE(v0, nullptr);
    const Vec3 before = v0->pos;

    const Transform xf = Transform::translation(Vec3{1.0, 0.0, 0.0});
    EXPECT_FALSE(model.transformVertices({v0->id, 99999}, xf));

    const Vec3 after = model.vertex(v0->id)->pos;
    EXPECT_TRUE(almostEqual(after, before, 1e-12));
}

TEST(TransformVerticesTest, IdentityTransformIsRejectedWithoutMutation) {
    Model model;
    model.addEdge(Vec3{0.0, 0.0, 0.0}, Vec3{1.0, 0.0, 0.0});
    const auto* v0 = model.findVertex(Vec3{0.0, 0.0, 0.0});
    ASSERT_NE(v0, nullptr);
    const Vec3 before = v0->pos;

    EXPECT_FALSE(model.transformVertices({v0->id}, Transform::identity()));

    const Vec3 after = model.vertex(v0->id)->pos;
    EXPECT_TRUE(almostEqual(after, before, 1e-12));
}

}  // namespace
