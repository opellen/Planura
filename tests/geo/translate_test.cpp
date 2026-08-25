#include <geo/entity.h>
#include <geo/model.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include <gtest/gtest.h>

namespace {

using plnr::geo::collectVertices;
using plnr::geo::EntityKind;
using plnr::geo::ExtrudeResult;
using plnr::geo::Id;
using plnr::geo::kInvalidId;
using plnr::geo::Model;
using plnr::geo::Vec3;

Id buildRectangleFace(Model& model, double width = 4.0, double depth = 3.0) {
    const Vec3 p0{0.0, 0.0, 0.0};
    const Vec3 p1{width, 0.0, 0.0};
    const Vec3 p2{width, depth, 0.0};
    const Vec3 p3{0.0, depth, 0.0};

    model.addEdge(p0, p1);
    model.addEdge(p1, p2);
    model.addEdge(p2, p3);
    const auto closing = model.addEdge(p3, p0);
    EXPECT_TRUE(closing.created);
    EXPECT_EQ(closing.newFaces.size(), 1u);
    return closing.newFaces.empty() ? kInvalidId : closing.newFaces[0];
}

TEST(EntityTest, CollectVerticesForVertexKind) {
    Model model;
    model.addEdge(Vec3{0.0, 0.0, 0.0}, Vec3{1.0, 0.0, 0.0});
    const auto* v0 = model.findVertex(Vec3{0.0, 0.0, 0.0});
    ASSERT_NE(v0, nullptr);

    const auto result = collectVertices(model, EntityKind::Vertex, v0->id);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0], v0->id);

    EXPECT_TRUE(collectVertices(model, EntityKind::Vertex, kInvalidId).empty());
    EXPECT_TRUE(collectVertices(model, EntityKind::Vertex, 99999).empty());
}

TEST(EntityTest, CollectVerticesForEdgeKind) {
    Model model;
    const auto e1 = model.addEdge(Vec3{0.0, 0.0, 0.0}, Vec3{1.0, 0.0, 0.0});
    ASSERT_TRUE(e1.created);

    const auto* v0 = model.findVertex(Vec3{0.0, 0.0, 0.0});
    const auto* v1 = model.findVertex(Vec3{1.0, 0.0, 0.0});
    ASSERT_NE(v0, nullptr);
    ASSERT_NE(v1, nullptr);

    auto result = collectVertices(model, EntityKind::Edge, e1.edge);
    ASSERT_EQ(result.size(), 2u);
    std::sort(result.begin(), result.end());
    std::vector<Id> expected{v0->id, v1->id};
    std::sort(expected.begin(), expected.end());
    EXPECT_EQ(result, expected);

    EXPECT_TRUE(collectVertices(model, EntityKind::Edge, 99999).empty());
}

TEST(EntityTest, CollectVerticesForFaceKind) {
    Model model;
    const Id faceId = buildRectangleFace(model);
    ASSERT_NE(faceId, kInvalidId);

    const auto result = collectVertices(model, EntityKind::Face, faceId);
    EXPECT_EQ(result.size(), 4u);
    EXPECT_EQ(result, model.faceVertexLoop(faceId));

    EXPECT_TRUE(collectVertices(model, EntityKind::Face, 99999).empty());
}

TEST(TranslateTest, TranslatingCapLoopMovesExactlyAndUpdatesOnlyAdjacentFaces) {
    Model model;
    const Id baseFaceId = buildRectangleFace(model);
    ASSERT_NE(baseFaceId, kInvalidId);

    const ExtrudeResult extrude = model.extrudeFace(baseFaceId, 2.0);
    ASSERT_TRUE(extrude.ok);

    // The bottom face is whichever survives that is neither the cap nor a side.
    Id bottomFaceId = kInvalidId;
    for (const auto& [id, face] : model.faces()) {
        if (id == extrude.capFace) {
            continue;
        }
        if (std::find(extrude.sideFaces.begin(), extrude.sideFaces.end(), id) != extrude.sideFaces.end()) {
            continue;
        }
        bottomFaceId = id;
    }
    ASSERT_NE(bottomFaceId, kInvalidId);

    const std::vector<Id> capLoop = collectVertices(model, EntityKind::Face, extrude.capFace);
    ASSERT_EQ(capLoop.size(), 4u);

    // A shear with both x and y components is needed so that all 4 walls
    // tilt: a pure x-shear leaves the y=0/y=depth walls exactly planar (they
    // never leave their own plane), so only the x=0/x=width walls would tilt.
    const Vec3 delta{1.0, 1.0, 0.0};

    std::vector<Vec3> expectedPositions;
    std::vector<Vec3> sideNormalsBefore;
    for (Id v : capLoop) {
        expectedPositions.push_back(model.vertex(v)->pos + delta);
    }
    for (Id sideId : extrude.sideFaces) {
        sideNormalsBefore.push_back(model.face(sideId)->normal);
    }

    ASSERT_TRUE(model.translateVertices(capLoop, delta));

    for (std::size_t i = 0; i < capLoop.size(); ++i) {
        const Vec3 pos = model.vertex(capLoop[i])->pos;
        EXPECT_NEAR(pos.x, expectedPositions[i].x, 1e-9);
        EXPECT_NEAR(pos.y, expectedPositions[i].y, 1e-9);
        EXPECT_NEAR(pos.z, expectedPositions[i].z, 1e-9);
    }

    // Cap and bottom loops each moved (or didn't) rigidly/uniformly, so their
    // own Newell normals are unchanged: still +/-Z.
    const auto* capFace = model.face(extrude.capFace);
    ASSERT_NE(capFace, nullptr);
    EXPECT_NEAR(std::fabs(capFace->normal.z), 1.0, 1e-9);
    EXPECT_NEAR(capFace->normal.x, 0.0, 1e-9);
    EXPECT_NEAR(capFace->normal.y, 0.0, 1e-9);

    const auto* bottomFace = model.face(bottomFaceId);
    ASSERT_NE(bottomFace, nullptr);
    EXPECT_NEAR(std::fabs(bottomFace->normal.z), 1.0, 1e-9);
    EXPECT_NEAR(bottomFace->normal.x, 0.0, 1e-9);
    EXPECT_NEAR(bottomFace->normal.y, 0.0, 1e-9);

    // Every side face is adjacent to 2 cap vertices, so all 4 got recomputed
    // and are no longer the original axis-aligned normal.
    for (std::size_t i = 0; i < extrude.sideFaces.size(); ++i) {
        const auto* side = model.face(extrude.sideFaces[i]);
        ASSERT_NE(side, nullptr);
        EXPECT_GT(distance(side->normal, sideNormalsBefore[i]), 0.05);
    }
}

TEST(TranslateTest, ZeroDeltaIsRejectedWithoutMutation) {
    Model model;
    model.addEdge(Vec3{0.0, 0.0, 0.0}, Vec3{1.0, 0.0, 0.0});
    const auto* v0 = model.findVertex(Vec3{0.0, 0.0, 0.0});
    ASSERT_NE(v0, nullptr);
    const Vec3 before = v0->pos;

    EXPECT_FALSE(model.translateVertices({v0->id}, Vec3{0.0, 0.0, 0.0}));

    const Vec3 after = model.vertex(v0->id)->pos;
    EXPECT_NEAR(after.x, before.x, 1e-12);
    EXPECT_NEAR(after.y, before.y, 1e-12);
    EXPECT_NEAR(after.z, before.z, 1e-12);
}

TEST(TranslateTest, UnknownVertexIdIsRejectedWithoutMutation) {
    Model model;
    model.addEdge(Vec3{0.0, 0.0, 0.0}, Vec3{1.0, 0.0, 0.0});
    const auto* v0 = model.findVertex(Vec3{0.0, 0.0, 0.0});
    ASSERT_NE(v0, nullptr);
    const Vec3 before = v0->pos;

    EXPECT_FALSE(model.translateVertices({v0->id, 99999}, Vec3{1.0, 0.0, 0.0}));

    const Vec3 after = model.vertex(v0->id)->pos;
    EXPECT_NEAR(after.x, before.x, 1e-12);
    EXPECT_NEAR(after.y, before.y, 1e-12);
    EXPECT_NEAR(after.z, before.z, 1e-12);
}

}  // namespace
