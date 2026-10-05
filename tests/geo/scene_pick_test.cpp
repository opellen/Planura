#include <geo/scene_pick.h>

#include <array>

#include <gtest/gtest.h>

namespace {

using plnr::geo::Definition;
using plnr::geo::Frustum;
using plnr::geo::frustumFromCornerRays;
using plnr::geo::Id;
using plnr::geo::kInvalidId;
using plnr::geo::kRootDefinitionId;
using plnr::geo::Model;
using plnr::geo::pickScene;
using plnr::geo::PickKind;
using plnr::geo::PickOptions;
using plnr::geo::Ray;
using plnr::geo::RegionMode;
using plnr::geo::RegionPick;
using plnr::geo::regionPickScene;
using plnr::geo::Scene;
using plnr::geo::ScenePickResult;
using plnr::geo::Transform;
using plnr::geo::Vec3;

// 4-vertex, 4-edge, 1-face rectangle in the z=origin.z plane.
Id buildRectangleFace(Model& model, Vec3 origin = {0.0, 0.0, 0.0}, double width = 4.0, double depth = 3.0) {
    const Vec3 p0 = origin;
    const Vec3 p1 = origin + Vec3{width, 0.0, 0.0};
    const Vec3 p2 = origin + Vec3{width, depth, 0.0};
    const Vec3 p3 = origin + Vec3{0.0, depth, 0.0};

    model.addEdge(p0, p1);
    model.addEdge(p1, p2);
    model.addEdge(p2, p3);
    const auto closing = model.addEdge(p3, p0);
    return closing.newFaces.empty() ? kInvalidId : closing.newFaces[0];
}

// Straight-down ray at (x, y) from height z (top-down camera convention).
Ray downRayAt(double x, double y, double z) {
    return Ray{Vec3{x, y, z}, Vec3{0.0, 0.0, -1.0}};
}

// 4 converging corner rays for a screen-axis-aligned drag rectangle
// [minX,maxX] x [minY,maxY] projected onto z = 0, eye above center looking
// down.
std::array<Ray, 4> cornerRaysAbove(double minX, double maxX, double minY, double maxY) {
    const Vec3 eye{(minX + maxX) / 2.0, (minY + maxY) / 2.0, 10.0};
    const Vec3 tl{minX, maxY, 0.0};
    const Vec3 tr{maxX, maxY, 0.0};
    const Vec3 br{maxX, minY, 0.0};
    const Vec3 bl{minX, minY, 0.0};
    return {
        Ray{eye, plnr::geo::normalized(tl - eye)},
        Ray{eye, plnr::geo::normalized(tr - eye)},
        Ray{eye, plnr::geo::normalized(br - eye)},
        Ray{eye, plnr::geo::normalized(bl - eye)},
    };
}

TEST(ScenePickTest, PickInstancesFalseIgnoresTheInstanceButTrueFindsItsFace) {
    Scene scene;
    const Id defId = scene.createDefinition("Def", /*isGroup=*/true);
    Definition* def = scene.definition(defId);
    const Id faceId = buildRectangleFace(def->model, Vec3{0.0, 0.0, 0.0});
    ASSERT_NE(faceId, kInvalidId);
    const Id instId = scene.addInstance(kRootDefinitionId, defId, Transform::translation({10.0, 0.0, 0.0}), "Inst");
    ASSERT_NE(instId, kInvalidId);

    // World-space face center: local (2, 1.5, 0) + translation (10,0,0).
    const Ray ray = downRayAt(12.0, 1.5, 5.0);
    const PickOptions opts{0.1, 0.1};

    const ScenePickResult ignored = pickScene(scene, ray, opts, /*pickInstances=*/false, {});
    EXPECT_EQ(ignored.kind, PickKind::None);

    const ScenePickResult found = pickScene(scene, ray, opts, /*pickInstances=*/true, {});
    EXPECT_EQ(found.kind, PickKind::Face);
    EXPECT_EQ(found.id, faceId);
    EXPECT_EQ(found.instanceId, instId);
    EXPECT_NEAR(found.point.x, 12.0, 1e-9);
    EXPECT_NEAR(found.point.y, 1.5, 1e-9);
    EXPECT_NEAR(found.point.z, 0.0, 1e-9);
}

TEST(ScenePickTest, NearestWinsRootFaceInFrontOfInstance) {
    Scene scene;
    // Root face at world z=1, directly above the instance's face at z=0 --
    // same (x,y) footprint, so the ray hits both candidates.
    const Id rootFaceId = buildRectangleFace(scene.root().model, Vec3{10.0, 0.0, 1.0});
    ASSERT_NE(rootFaceId, kInvalidId);

    const Id defId = scene.createDefinition("Def", /*isGroup=*/true);
    Definition* def = scene.definition(defId);
    buildRectangleFace(def->model, Vec3{0.0, 0.0, 0.0});
    scene.addInstance(kRootDefinitionId, defId, Transform::translation({10.0, 0.0, 0.0}), "Inst");

    const Ray ray = downRayAt(12.0, 1.5, 10.0);
    const PickOptions opts{0.1, 0.1};

    const ScenePickResult result = pickScene(scene, ray, opts, /*pickInstances=*/true, {});
    EXPECT_EQ(result.kind, PickKind::Face);
    EXPECT_EQ(result.id, rootFaceId);
    EXPECT_EQ(result.instanceId, kInvalidId);  // root hit, not the instance behind it
    EXPECT_NEAR(result.depth, 9.0, 1e-9);
}

TEST(ScenePickTest, NearestWinsInstanceInFrontOfRootFace) {
    Scene scene;
    // Root face at world z=0; the instance's local face (z=0) is placed one
    // unit closer to the ray origin via its transform's z translation.
    const Id rootFaceId = buildRectangleFace(scene.root().model, Vec3{10.0, 0.0, 0.0});
    ASSERT_NE(rootFaceId, kInvalidId);
    (void)rootFaceId;

    const Id defId = scene.createDefinition("Def", /*isGroup=*/true);
    Definition* def = scene.definition(defId);
    const Id instFaceId = buildRectangleFace(def->model, Vec3{0.0, 0.0, 0.0});
    const Id instId = scene.addInstance(kRootDefinitionId, defId, Transform::translation({10.0, 0.0, 1.0}), "Inst");

    const Ray ray = downRayAt(12.0, 1.5, 10.0);
    const PickOptions opts{0.1, 0.1};

    const ScenePickResult result = pickScene(scene, ray, opts, /*pickInstances=*/true, {});
    EXPECT_EQ(result.kind, PickKind::Face);
    EXPECT_EQ(result.id, instFaceId);
    EXPECT_EQ(result.instanceId, instId);
    EXPECT_NEAR(result.depth, 9.0, 1e-9);
}

TEST(ScenePickTest, NestedInstanceStillReportsTopLevelInstanceId) {
    Scene scene;
    const Id innerDefId = scene.createDefinition("Inner", /*isGroup=*/true);
    Definition* innerDef = scene.definition(innerDefId);
    const Id innerFaceId = buildRectangleFace(innerDef->model, Vec3{0.0, 0.0, 0.0});
    ASSERT_NE(innerFaceId, kInvalidId);

    const Id outerDefId = scene.createDefinition("Outer", /*isGroup=*/true);
    scene.addInstance(outerDefId, innerDefId, Transform::translation({5.0, 0.0, 0.0}), "NestedInst");

    const Id topInstId =
        scene.addInstance(kRootDefinitionId, outerDefId, Transform::translation({10.0, 0.0, 0.0}), "TopInst");
    ASSERT_NE(topInstId, kInvalidId);

    // World face center: local (2, 1.5, 0) + nested (5,0,0) + top (10,0,0).
    const Ray ray = downRayAt(17.0, 1.5, 5.0);
    const PickOptions opts{0.1, 0.1};

    const ScenePickResult result = pickScene(scene, ray, opts, /*pickInstances=*/true, {});
    EXPECT_EQ(result.kind, PickKind::Face);
    EXPECT_EQ(result.id, innerFaceId);
    EXPECT_EQ(result.instanceId, topInstId);  // top-level id, not the nested one
}

TEST(ScenePickTest, RegionWindowFullyContainingInstanceListsIt) {
    Scene scene;
    const Id defId = scene.createDefinition("Def", /*isGroup=*/true);
    Definition* def = scene.definition(defId);
    buildRectangleFace(def->model, Vec3{0.0, 0.0, 0.0});  // local (0,0)-(4,3)
    const Id instId = scene.addInstance(kRootDefinitionId, defId, Transform::translation({10.0, 0.0, 0.0}), "Inst");

    // World footprint (10,0)-(14,3); frustum covers (9,-1)-(15,4).
    const Frustum f = frustumFromCornerRays(cornerRaysAbove(9.0, 15.0, -1.0, 4.0));
    RegionPick rootPick;
    std::vector<Id> instances;
    regionPickScene(scene, f, RegionMode::Window, {}, rootPick, instances, {});

    ASSERT_EQ(instances.size(), 1u);
    EXPECT_EQ(instances[0], instId);
}

TEST(ScenePickTest, RegionWindowHalfContainingInstanceDoesNotListItButCrossingDoes) {
    Scene scene;
    const Id defId = scene.createDefinition("Def", /*isGroup=*/true);
    Definition* def = scene.definition(defId);
    buildRectangleFace(def->model, Vec3{0.0, 0.0, 0.0});  // local (0,0)-(4,3)
    const Id instId = scene.addInstance(kRootDefinitionId, defId, Transform::translation({10.0, 0.0, 0.0}), "Inst");

    // World footprint (10,0)-(14,3); frustum covers only (9,-1)-(12,4) --
    // vertices at x=10 are inside, x=14 are not.
    const Frustum f = frustumFromCornerRays(cornerRaysAbove(9.0, 12.0, -1.0, 4.0));

    RegionPick windowPick;
    std::vector<Id> windowInstances;
    regionPickScene(scene, f, RegionMode::Window, {}, windowPick, windowInstances, {});
    EXPECT_TRUE(windowInstances.empty());

    RegionPick crossingPick;
    std::vector<Id> crossingInstances;
    regionPickScene(scene, f, RegionMode::Crossing, {}, crossingPick, crossingInstances, {});
    ASSERT_EQ(crossingInstances.size(), 1u);
    EXPECT_EQ(crossingInstances[0], instId);
}

// -- Editing-context-aware picking -------------------------------------

TEST(ScenePickTest, ContextPathPicksTheContextDefinitionsOwnGeometryNotRoot) {
    Scene scene;
    // Root has its own face directly beneath the same ray -- must be
    // ignored once contextPath names the group instead.
    const Id rootFaceId = buildRectangleFace(scene.root().model, Vec3{0.0, 0.0, 0.0});
    ASSERT_NE(rootFaceId, kInvalidId);

    const Id defId = scene.createDefinition("Def", /*isGroup=*/true);
    Definition* def = scene.definition(defId);
    const Id groupFaceId = buildRectangleFace(def->model, Vec3{0.0, 0.0, 0.0});
    ASSERT_NE(groupFaceId, kInvalidId);
    const Id instId = scene.addInstance(kRootDefinitionId, defId, Transform::identity(), "Inst");

    const Ray ray = downRayAt(2.0, 1.5, 5.0);
    const PickOptions opts{0.1, 0.1};

    // Root context (empty path): the root's own face wins, unaffected by
    // the group instance sitting at the identical footprint.
    const ScenePickResult rootResult = pickScene(scene, ray, opts, /*pickInstances=*/true, {});
    EXPECT_EQ(rootResult.kind, PickKind::Face);
    EXPECT_EQ(rootResult.id, rootFaceId);
    EXPECT_EQ(rootResult.instanceId, kInvalidId);

    // Inside the group's editing context: the group's OWN face picks raw
    // (instanceId back to kInvalidId, same as a root hit would report), and
    // the root's face at the same screen position is click-transparent.
    const ScenePickResult ctxResult = pickScene(scene, ray, opts, /*pickInstances=*/true, {instId});
    EXPECT_EQ(ctxResult.kind, PickKind::Face);
    EXPECT_EQ(ctxResult.id, groupFaceId);
    EXPECT_EQ(ctxResult.instanceId, kInvalidId);
}

TEST(ScenePickTest, ContextPathStillPicksItsOwnChildInstanceAsAUnit) {
    Scene scene;
    const Id outerDefId = scene.createDefinition("Outer", /*isGroup=*/true);
    Definition* outerDef = scene.definition(outerDefId);

    const Id innerDefId = scene.createDefinition("Inner", /*isGroup=*/true);
    Definition* innerDef = scene.definition(innerDefId);
    const Id innerFaceId = buildRectangleFace(innerDef->model, Vec3{0.0, 0.0, 0.0});
    ASSERT_NE(innerFaceId, kInvalidId);

    const Id childInstId = scene.addInstance(outerDefId, innerDefId, Transform::translation({5.0, 0.0, 0.0}), "Child");
    ASSERT_NE(childInstId, kInvalidId);

    const Id topInstId = scene.addInstance(kRootDefinitionId, outerDefId, Transform::identity(), "Top");
    ASSERT_NE(topInstId, kInvalidId);

    // World face center: local (2, 1.5, 0) + child's translation (5,0,0).
    const Ray ray = downRayAt(7.0, 1.5, 5.0);
    const PickOptions opts{0.1, 0.1};

    // Outside any context, this hit belongs to the top-level instance.
    const ScenePickResult rootResult = pickScene(scene, ray, opts, /*pickInstances=*/true, {});
    EXPECT_EQ(rootResult.instanceId, topInstId);

    // Once inside topInstId's context, the SAME hit resolves to the child
    // instance one level down instead (relative to the current context, not
    // the scene's global top level).
    const ScenePickResult ctxResult = pickScene(scene, ray, opts, /*pickInstances=*/true, {topInstId});
    EXPECT_EQ(ctxResult.kind, PickKind::Face);
    EXPECT_EQ(ctxResult.id, innerFaceId);
    EXPECT_EQ(ctxResult.instanceId, childInstId);
}

TEST(ScenePickTest, UnresolvableContextPathPicksNothing) {
    Scene scene;
    buildRectangleFace(scene.root().model, Vec3{0.0, 0.0, 0.0});
    const Ray ray = downRayAt(2.0, 1.5, 5.0);
    const PickOptions opts{0.1, 0.1};

    const ScenePickResult result = pickScene(scene, ray, opts, /*pickInstances=*/true, {999999});
    EXPECT_EQ(result.kind, PickKind::None);

    RegionPick regionResult;
    std::vector<Id> instances;
    const Frustum f = frustumFromCornerRays(cornerRaysAbove(-1.0, 5.0, -1.0, 4.0));
    regionPickScene(scene, f, RegionMode::Window, {}, regionResult, instances, {999999});
    EXPECT_TRUE(regionResult.vertices.empty());
    EXPECT_TRUE(regionResult.edges.empty());
    EXPECT_TRUE(regionResult.faces.empty());
    EXPECT_TRUE(instances.empty());
}

TEST(ScenePickTest, RegionPickWithContextPathQualifiesOnlyItsOwnChildInstance) {
    Scene scene;
    const Id defId = scene.createDefinition("Def", /*isGroup=*/true);
    Definition* def = scene.definition(defId);
    buildRectangleFace(def->model, Vec3{0.0, 0.0, 0.0});  // local (0,0)-(4,3)
    const Id instId = scene.addInstance(kRootDefinitionId, defId, Transform::translation({10.0, 0.0, 0.0}), "Inst");

    // Root's own face, well away from the group -- must not appear when the
    // region query is scoped to the group's context.
    buildRectangleFace(scene.root().model, Vec3{100.0, 100.0, 0.0});

    // World footprint (10,0)-(14,3); frustum covers (9,-1)-(15,4).
    const Frustum f = frustumFromCornerRays(cornerRaysAbove(9.0, 15.0, -1.0, 4.0));

    RegionPick rootScopedPick;
    std::vector<Id> rootScopedInstances;
    regionPickScene(scene, f, RegionMode::Window, {}, rootScopedPick, rootScopedInstances, {});
    ASSERT_EQ(rootScopedInstances.size(), 1u);
    EXPECT_EQ(rootScopedInstances[0], instId);

    // Same frustum, scoped inside the group's OWN context: its own geometry
    // (translated to local (0,0)-(4,3), inside the same world frustum once
    // shifted back) qualifies directly -- no child instance to report.
    RegionPick ctxPick;
    std::vector<Id> ctxInstances;
    regionPickScene(scene, f, RegionMode::Window, {}, ctxPick, ctxInstances, {instId});
    EXPECT_TRUE(ctxInstances.empty());
    EXPECT_EQ(ctxPick.faces.size(), 1u);
}

}  // namespace
