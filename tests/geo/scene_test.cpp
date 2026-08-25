#include <geo/scene.h>

#include <gtest/gtest.h>

namespace {

using plnr::geo::almostEqual;
using plnr::geo::Definition;
using plnr::geo::Id;
using plnr::geo::Instance;
using plnr::geo::kInvalidId;
using plnr::geo::kRootDefinitionId;
using plnr::geo::Scene;
using plnr::geo::Transform;
using plnr::geo::Vec3;

// -- Transform ---------------------------------------------------------

TEST(TransformTest, IdentityApplyIsNoOp) {
    const Transform id = Transform::identity();
    const Vec3 p{1.0, 2.0, 3.0};
    EXPECT_TRUE(almostEqual(id.apply(p), p));
    EXPECT_TRUE(almostEqual(id.applyVector(p), p));
}

TEST(TransformTest, IdentityComposedWithAnythingIsThatTransform) {
    const Transform id = Transform::identity();
    const Transform t = Transform::translation(Vec3{5.0, -2.0, 1.0});
    EXPECT_TRUE(id.composed(t).almostEqual(t));
    EXPECT_TRUE(t.composed(id).almostEqual(t));
}

TEST(TransformTest, IdentityInverseRoundTripsToIdentity) {
    const Transform id = Transform::identity();
    EXPECT_TRUE(id.inverse().almostEqual(id));
}

TEST(TransformTest, TranslationApplyShiftsPoint) {
    const Transform t = Transform::translation(Vec3{1.0, 2.0, 3.0});
    const Vec3 p{10.0, 0.0, -5.0};
    EXPECT_TRUE(almostEqual(t.apply(p), Vec3{11.0, 2.0, -2.0}));
}

TEST(TransformTest, TranslationInverseUndoesTheShift) {
    const Transform t = Transform::translation(Vec3{1.0, 2.0, 3.0});
    const Vec3 p{10.0, 0.0, -5.0};
    const Vec3 shifted = t.apply(p);
    EXPECT_TRUE(almostEqual(t.inverse().apply(shifted), p));
    // The inverse of a pure translation is translation by -t.
    EXPECT_TRUE(t.inverse().almostEqual(Transform::translation(Vec3{-1.0, -2.0, -3.0})));
}

TEST(TransformTest, ApplyVectorIgnoresTranslation) {
    const Transform t = Transform::translation(Vec3{100.0, 100.0, 100.0});
    const Vec3 dir{1.0, 0.0, 0.0};
    // A pure translation's linear part is identity, so a direction is
    // unchanged -- unlike apply(), which would shift it by t.
    EXPECT_TRUE(almostEqual(t.applyVector(dir), dir));
    EXPECT_FALSE(almostEqual(t.apply(dir), dir));
}

// composed(A, B): A.composed(B) applies B first, then A. Pinned by comparing
// against the step-by-step order explicitly, not just the combined offset
// (which happens to be commutative for pure translations).
TEST(TransformTest, ComposedAppliesInnerOperandFirst) {
    const Transform a = Transform::translation(Vec3{1.0, 0.0, 0.0});
    const Transform b = Transform::translation(Vec3{0.0, 10.0, 0.0});
    const Vec3 p{0.0, 0.0, 0.0};

    const Transform aThenB = a.composed(b);  // b (inner) first, then a (outer)
    EXPECT_TRUE(almostEqual(aThenB.apply(p), a.apply(b.apply(p))));
    EXPECT_TRUE(almostEqual(aThenB.apply(p), Vec3{1.0, 10.0, 0.0}));
}

TEST(TransformTest, InverseOfGeneralInvertibleMatrixRoundTripsToIdentity) {
    // Hand-built non-trivial linear part (swaps x/y, scales by 2) plus a
    // non-zero translation -- exercises the general 3x3 inverse path, not
    // the trivial identity-linear-part shortcut a pure translation would take.
    Transform xf;
    xf.col0 = Vec3{0.0, 2.0, 0.0};
    xf.col1 = Vec3{2.0, 0.0, 0.0};
    xf.col2 = Vec3{0.0, 0.0, 2.0};
    xf.t = Vec3{1.0, 2.0, 3.0};

    const Transform inv = xf.inverse();
    EXPECT_TRUE(inv.composed(xf).almostEqual(Transform::identity(), 1e-9));
    EXPECT_TRUE(xf.composed(inv).almostEqual(Transform::identity(), 1e-9));

    const Vec3 p{7.0, -4.0, 2.5};
    EXPECT_TRUE(almostEqual(inv.apply(xf.apply(p)), p, 1e-9));
}

// -- Scene ---------------------------------------------------------------

TEST(SceneTest, RootExistsWithFixedIdAndName) {
    Scene scene;
    EXPECT_EQ(scene.root().id, kRootDefinitionId);
    EXPECT_EQ(scene.root().name, "Model");
    EXPECT_FALSE(scene.root().isGroup);
    EXPECT_TRUE(scene.root().children.empty());

    const Definition* viaLookup = scene.definition(kRootDefinitionId);
    ASSERT_NE(viaLookup, nullptr);
    EXPECT_EQ(viaLookup, &scene.root());
}

TEST(SceneTest, CreateDefinitionAndAddInstanceRoundTrip) {
    Scene scene;
    const Id defId = scene.createDefinition("Chair", /*isGroup=*/false);
    ASSERT_NE(defId, kInvalidId);

    const Definition* def = scene.definition(defId);
    ASSERT_NE(def, nullptr);
    EXPECT_EQ(def->name, "Chair");
    EXPECT_FALSE(def->isGroup);

    const Transform xf = Transform::translation(Vec3{1.0, 2.0, 3.0});
    const Id instId = scene.addInstance(kRootDefinitionId, defId, xf, "Chair #1");
    ASSERT_NE(instId, kInvalidId);

    Instance* inst = scene.findInstance(kRootDefinitionId, instId);
    ASSERT_NE(inst, nullptr);
    EXPECT_EQ(inst->id, instId);
    EXPECT_EQ(inst->definitionId, defId);
    EXPECT_EQ(inst->name, "Chair #1");
    EXPECT_TRUE(inst->transform.almostEqual(xf));

    // const overloads resolve the same records.
    const Scene& constScene = scene;
    EXPECT_NE(constScene.definition(defId), nullptr);
    EXPECT_NE(constScene.findInstance(kRootDefinitionId, instId), nullptr);
}

TEST(SceneTest, AddInstanceWithUnknownDefinitionReturnsInvalidId) {
    Scene scene;
    const Id defId = scene.createDefinition("Table", false);

    EXPECT_EQ(scene.addInstance(kRootDefinitionId, /*childDefId=*/999999, Transform::identity(), "x"), kInvalidId);
    EXPECT_EQ(scene.addInstance(/*parentDefId=*/999999, defId, Transform::identity(), "x"), kInvalidId);
}

TEST(SceneTest, AddInstanceSelfReferenceReturnsInvalidId) {
    Scene scene;
    const Id defId = scene.createDefinition("Loop", false);

    EXPECT_EQ(scene.addInstance(defId, defId, Transform::identity(), "self"), kInvalidId);
    // Root self-referencing itself is guarded the same way.
    EXPECT_EQ(scene.addInstance(kRootDefinitionId, kRootDefinitionId, Transform::identity(), "self"), kInvalidId);
}

TEST(SceneTest, RemoveInstanceWorksAndUnknownIsFalse) {
    Scene scene;
    const Id defId = scene.createDefinition("Box", false);
    const Id instId = scene.addInstance(kRootDefinitionId, defId, Transform::identity(), "Box #1");
    ASSERT_NE(instId, kInvalidId);

    EXPECT_TRUE(scene.removeInstance(kRootDefinitionId, instId));
    EXPECT_EQ(scene.findInstance(kRootDefinitionId, instId), nullptr);

    // Already removed -- second call is a no-op false.
    EXPECT_FALSE(scene.removeInstance(kRootDefinitionId, instId));
    // Unknown parent definition.
    EXPECT_FALSE(scene.removeInstance(/*parentDefId=*/999999, instId));
}

TEST(SceneTest, IdsAreMonotonicAcrossDefinitionsAndInstances) {
    Scene scene;
    const Id def1 = scene.createDefinition("A", false);
    const Id inst1 = scene.addInstance(kRootDefinitionId, def1, Transform::identity(), "a1");
    const Id def2 = scene.createDefinition("B", false);
    const Id inst2 = scene.addInstance(kRootDefinitionId, def2, Transform::identity(), "b1");

    // Root is fixed at 1; every subsequent id (definition or instance, in
    // whatever order they're created) is strictly increasing and unique.
    EXPECT_GT(def1, kRootDefinitionId);
    EXPECT_GT(inst1, def1);
    EXPECT_GT(def2, inst1);
    EXPECT_GT(inst2, def2);
}

}  // namespace
