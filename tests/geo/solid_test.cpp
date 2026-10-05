#include <geo/solid.h>

#include <vector>

#include <gtest/gtest.h>

#include <geo/scene.h>

namespace {

using plnr::geo::Definition;
using plnr::geo::Id;
using plnr::geo::isSolid;
using plnr::geo::isSolidDefinition;
using plnr::geo::Model;
using plnr::geo::solidVolume;
using plnr::geo::Vec3;

// Adds a closed box's 12 wire edges + 6 explicit faces spanning [min,max].
// outward=false reverses every loop/normal (inside-out shell). omitFace in
// [0,5] skips one face, leaving its edges wire.
void makeBox(Model& model, Vec3 minP, Vec3 maxP, bool outward = true, int omitFace = -1) {
    const Vec3 v0p{minP.x, minP.y, minP.z};
    const Vec3 v1p{maxP.x, minP.y, minP.z};
    const Vec3 v2p{maxP.x, maxP.y, minP.z};
    const Vec3 v3p{minP.x, maxP.y, minP.z};
    const Vec3 v4p{minP.x, minP.y, maxP.z};
    const Vec3 v5p{maxP.x, minP.y, maxP.z};
    const Vec3 v6p{maxP.x, maxP.y, maxP.z};
    const Vec3 v7p{minP.x, maxP.y, maxP.z};

    // The 12 edges: bottom loop, top loop, 4 verticals.
    model.addEdge(v0p, v3p, false);
    model.addEdge(v3p, v2p, false);
    model.addEdge(v2p, v1p, false);
    model.addEdge(v1p, v0p, false);
    model.addEdge(v4p, v5p, false);
    model.addEdge(v5p, v6p, false);
    model.addEdge(v6p, v7p, false);
    model.addEdge(v7p, v4p, false);
    model.addEdge(v0p, v4p, false);
    model.addEdge(v1p, v5p, false);
    model.addEdge(v2p, v6p, false);
    model.addEdge(v3p, v7p, false);

    // Each face's outward-facing loop + normal, derived via the right-hand
    // rule (cross(e1, e2) matches the listed normal).
    const std::vector<Vec3> bottom = {v0p, v3p, v2p, v1p};
    const Vec3 bottomN{0.0, 0.0, -1.0};
    const std::vector<Vec3> top = {v4p, v5p, v6p, v7p};
    const Vec3 topN{0.0, 0.0, 1.0};
    const std::vector<Vec3> front = {v0p, v1p, v5p, v4p};
    const Vec3 frontN{0.0, -1.0, 0.0};
    const std::vector<Vec3> back = {v3p, v7p, v6p, v2p};
    const Vec3 backN{0.0, 1.0, 0.0};
    const std::vector<Vec3> left = {v0p, v4p, v7p, v3p};
    const Vec3 leftN{-1.0, 0.0, 0.0};
    const std::vector<Vec3> right = {v1p, v2p, v6p, v5p};
    const Vec3 rightN{1.0, 0.0, 0.0};

    const std::vector<Vec3>* loops[6] = {&bottom, &top, &front, &back, &left, &right};
    const Vec3 normals[6] = {bottomN, topN, frontN, backN, leftN, rightN};

    for (int i = 0; i < 6; ++i) {
        if (i == omitFace) continue;

        std::vector<Id> loopIds;
        Vec3 normal = normals[i];
        if (outward) {
            for (const Vec3& p : *loops[i]) {
                loopIds.push_back(model.findVertex(p)->id);
            }
        } else {
            for (auto it = loops[i]->rbegin(); it != loops[i]->rend(); ++it) {
                loopIds.push_back(model.findVertex(*it)->id);
            }
            normal = normal * -1.0;
        }
        model.addFaceOnLoop(loopIds, normal);
    }
}

TEST(SolidTest, ClosedBoxIsSolidWithAnalyticVolume) {
    Model model;
    makeBox(model, Vec3{0.0, 0.0, 0.0}, Vec3{2.0, 3.0, 4.0});

    const auto info = isSolid(model);
    EXPECT_TRUE(info.solid);
    EXPECT_FALSE(info.wireEdges);
    EXPECT_FALSE(info.nonManifoldVertex);
    EXPECT_FALSE(info.nonPositiveVolume);
    EXPECT_NEAR(solidVolume(model), 2.0 * 3.0 * 4.0, 1e-9);
}

TEST(SolidTest, BoxWithOneFaceOmittedHasWireEdges) {
    Model model;
    makeBox(model, Vec3{0.0, 0.0, 0.0}, Vec3{1.0, 1.0, 1.0}, /*outward=*/true, /*omitFace=*/1);  // omit top

    const auto info = isSolid(model);
    EXPECT_FALSE(info.solid);
    EXPECT_TRUE(info.wireEdges);
}

TEST(SolidTest, ClosedBoxWithDanglingEdgeHasWireEdges) {
    Model model;
    makeBox(model, Vec3{0.0, 0.0, 0.0}, Vec3{1.0, 1.0, 1.0});
    // A dangling extra edge to a brand-new vertex: closes no loop, both
    // half-edges stay wire.
    model.addEdge(Vec3{0.0, 0.0, 0.0}, Vec3{10.0, 10.0, 10.0});

    const auto info = isSolid(model);
    EXPECT_FALSE(info.solid);
    EXPECT_TRUE(info.wireEdges);
}

TEST(SolidTest, BowtieSharedCornerIsNonManifold) {
    Model model;
    makeBox(model, Vec3{0.0, 0.0, 0.0}, Vec3{1.0, 1.0, 1.0});
    // Second box's [1,1,1] corner merges (kMergeTol) with the first box's own
    // [1,1,1] corner -- two closed shells meeting at exactly one vertex.
    makeBox(model, Vec3{1.0, 1.0, 1.0}, Vec3{2.0, 2.0, 2.0});

    const auto info = isSolid(model);
    EXPECT_FALSE(info.solid);
    EXPECT_FALSE(info.wireEdges);
    EXPECT_TRUE(info.nonManifoldVertex);
}

TEST(SolidTest, GloballyReversedShellHasNonPositiveVolume) {
    Model model;
    makeBox(model, Vec3{0.0, 0.0, 0.0}, Vec3{1.0, 1.0, 1.0}, /*outward=*/false);

    const auto info = isSolid(model);
    EXPECT_FALSE(info.solid);
    EXPECT_FALSE(info.wireEdges);
    EXPECT_FALSE(info.nonManifoldVertex);
    EXPECT_TRUE(info.nonPositiveVolume);
    EXPECT_LE(solidVolume(model), 0.0);
}

TEST(SolidTest, HollowCubeVolumeIsOuterMinusInner) {
    Model model;
    makeBox(model, Vec3{0.0, 0.0, 0.0}, Vec3{4.0, 4.0, 4.0}, /*outward=*/true);
    makeBox(model, Vec3{1.0, 1.0, 1.0}, Vec3{3.0, 3.0, 3.0}, /*outward=*/false);

    const auto info = isSolid(model);
    EXPECT_TRUE(info.solid);
    EXPECT_NEAR(solidVolume(model), 64.0 - 8.0, 1e-9);
}

TEST(SolidTest, IsSolidDefinitionFalseWithChildInstanceTrueWithout) {
    plnr::geo::Scene scene;
    const Id boxDefId = scene.createDefinition("box", /*isGroup=*/true);
    Definition* boxDef = scene.definition(boxDefId);
    ASSERT_NE(boxDef, nullptr);
    makeBox(boxDef->model, Vec3{0.0, 0.0, 0.0}, Vec3{1.0, 1.0, 1.0});
    ASSERT_TRUE(isSolid(boxDef->model).solid);

    EXPECT_TRUE(isSolidDefinition(*boxDef));

    // Append a child Instance to the BOX definition's own children (not
    // root's) -- isSolidDefinition looks at def.children directly.
    const Id childDefId = scene.createDefinition("child", /*isGroup=*/true);
    scene.addInstance(boxDefId, childDefId, plnr::geo::Transform::identity(), "instance");
    boxDef = scene.definition(boxDefId);
    ASSERT_NE(boxDef, nullptr);
    EXPECT_FALSE(isSolidDefinition(*boxDef));
}

TEST(SolidTest, EmptyModelIsNotSolid) {
    Model model;
    EXPECT_FALSE(isSolid(model).solid);
}

}  // namespace
