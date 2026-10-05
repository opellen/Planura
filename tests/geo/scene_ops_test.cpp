#include <geo/scene_ops.h>

#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <geo/solid.h>

namespace {

using plnr::geo::EntityKind;
using plnr::geo::EntitySet;
using plnr::geo::ExtrudeResult;
using plnr::geo::Id;
using plnr::geo::kInvalidId;
using plnr::geo::Model;
using plnr::geo::Transform;
using plnr::geo::Vec3;

using plnr::geo::closureOf;
using plnr::geo::copyInto;
using plnr::geo::isSolid;
using plnr::geo::removeFrom;
using plnr::geo::solidVolume;

// 4-vertex, 4-edge, 1-face rectangle in the z=0 plane at the given origin.
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

// Closed, outward-oriented box: 12 edges (detectFaces=false) + 6 faces
// stated explicitly via addFaceOnLoop with outward normals -- required for
// winding fidelity, see the maintainer notes.
void makeOutwardBox(Model& model, Vec3 minP, Vec3 maxP) {
    const Vec3 v0p{minP.x, minP.y, minP.z};
    const Vec3 v1p{maxP.x, minP.y, minP.z};
    const Vec3 v2p{maxP.x, maxP.y, minP.z};
    const Vec3 v3p{minP.x, maxP.y, minP.z};
    const Vec3 v4p{minP.x, minP.y, maxP.z};
    const Vec3 v5p{maxP.x, minP.y, maxP.z};
    const Vec3 v6p{maxP.x, maxP.y, maxP.z};
    const Vec3 v7p{minP.x, maxP.y, maxP.z};

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

    const auto vid = [&](Vec3 p) { return model.findVertex(p)->id; };
    model.addFaceOnLoop({vid(v0p), vid(v3p), vid(v2p), vid(v1p)}, Vec3{0.0, 0.0, -1.0});
    model.addFaceOnLoop({vid(v4p), vid(v5p), vid(v6p), vid(v7p)}, Vec3{0.0, 0.0, 1.0});
    model.addFaceOnLoop({vid(v0p), vid(v1p), vid(v5p), vid(v4p)}, Vec3{0.0, -1.0, 0.0});
    model.addFaceOnLoop({vid(v3p), vid(v7p), vid(v6p), vid(v2p)}, Vec3{0.0, 1.0, 0.0});
    model.addFaceOnLoop({vid(v0p), vid(v4p), vid(v7p), vid(v3p)}, Vec3{-1.0, 0.0, 0.0});
    model.addFaceOnLoop({vid(v1p), vid(v2p), vid(v6p), vid(v5p)}, Vec3{1.0, 0.0, 0.0});
}

// Seeds closureOf with every face currently in model -- the same "whole
// Definition" seeding makeGroup/explode themselves use (closureOf cascades
// each face down to its own boundary edges and vertices).
EntitySet closeWholeModel(const Model& model) {
    std::vector<std::pair<EntityKind, Id>> seeds;
    for (const auto& [id, face] : model.faces()) {
        (void)face;
        seeds.emplace_back(EntityKind::Face, id);
    }
    return closureOf(model, seeds);
}

TEST(SceneOpsTest, ClosureOfAFaceIsTheFacePlusItsFourEdgesAndTheirVertices) {
    Model model;
    const Id faceId = buildRectangleFace(model);
    ASSERT_NE(faceId, kInvalidId);

    const EntitySet set = closureOf(model, {{EntityKind::Face, faceId}});

    ASSERT_EQ(set.faces.size(), 1u);
    EXPECT_EQ(set.faces[0], faceId);
    EXPECT_EQ(set.edges.size(), 4u);
    EXPECT_EQ(set.vertices.size(), 4u);
}

TEST(SceneOpsTest, ClosureOfAnEdgeIsTheEdgePlusItsTwoEndpoints) {
    Model model;
    const auto result = model.addEdge({0.0, 0.0, 0.0}, {1.0, 0.0, 0.0});
    ASSERT_TRUE(result.created);

    const EntitySet set = closureOf(model, {{EntityKind::Edge, result.edge}});

    EXPECT_TRUE(set.faces.empty());
    ASSERT_EQ(set.edges.size(), 1u);
    EXPECT_EQ(set.edges[0], result.edge);
    EXPECT_EQ(set.vertices.size(), 2u);
}

TEST(SceneOpsTest, ClosureOfAnUnknownSeedIsEmpty) {
    Model model;
    const EntitySet set = closureOf(model, {{EntityKind::Face, 999999}});

    EXPECT_TRUE(set.vertices.empty());
    EXPECT_TRUE(set.edges.empty());
    EXPECT_TRUE(set.faces.empty());
}

TEST(SceneOpsTest, CopyIntoRecreatesFaceAndAppliesTranslation) {
    Model src;
    const Id faceId = buildRectangleFace(src);
    ASSERT_NE(faceId, kInvalidId);
    const EntitySet set = closureOf(src, {{EntityKind::Face, faceId}});
    ASSERT_EQ(set.edges.size(), 4u);

    Model dst;
    const Transform xf = Transform::translation(Vec3{10.0, 20.0, 30.0});
    copyInto(dst, src, set, xf);

    EXPECT_EQ(dst.vertices().size(), 4u);
    EXPECT_EQ(dst.edges().size(), 4u);
    EXPECT_EQ(dst.faces().size(), 1u);

    // Every original corner landed at its source position plus the
    // translation -- ids are fresh (not asserted -- see copyInto's header
    // comment), but the geometry itself must match exactly.
    EXPECT_NE(dst.findVertex(Vec3{10.0, 20.0, 30.0}), nullptr);
    EXPECT_NE(dst.findVertex(Vec3{14.0, 20.0, 30.0}), nullptr);
    EXPECT_NE(dst.findVertex(Vec3{14.0, 23.0, 30.0}), nullptr);
    EXPECT_NE(dst.findVertex(Vec3{10.0, 23.0, 30.0}), nullptr);
}

// With edges ONLY in the set (no face named), copyInto fabricates NO face:
// every replay addEdge call uses detectFaces=false, so a face only comes
// back if explicitly named in `set.faces` -- see copyInto's header comment.
TEST(SceneOpsTest, CopyIntoWithEdgesOnlyFabricatesNoFaceEvenForAClosedBox) {
    Model src;
    const Id faceId = buildRectangleFace(src);
    ASSERT_NE(faceId, kInvalidId);
    const ExtrudeResult extrude = src.extrudeFace(faceId, 2.0);
    ASSERT_TRUE(extrude.ok);
    ASSERT_EQ(src.edges().size(), 12u);
    ASSERT_EQ(src.faces().size(), 6u);

    EntitySet set;
    for (const auto& [id, edge] : src.edges()) {
        (void)edge;
        set.edges.push_back(id);
    }

    Model dst;
    copyInto(dst, src, set, Transform::identity());

    EXPECT_EQ(dst.vertices().size(), 8u);
    EXPECT_EQ(dst.edges().size(), 12u);
    EXPECT_EQ(dst.faces().size(), 0u);
}

// A minimal (single-loop) version of the guarantee above: 4 edges forming
// one closed square loop, no face named in the set -- copyInto still creates
// zero faces, even though the loop is trivially closeable.
TEST(SceneOpsTest, CopyIntoOfAClosedSquareLoopWithNoFaceInTheSetFabricatesNoFace) {
    Model src;
    const Id faceId = buildRectangleFace(src);
    ASSERT_NE(faceId, kInvalidId);
    ASSERT_EQ(src.edges().size(), 4u);

    EntitySet set;
    for (const auto& [id, edge] : src.edges()) {
        (void)edge;
        set.edges.push_back(id);
    }
    ASSERT_TRUE(set.faces.empty());

    Model dst;
    copyInto(dst, src, set, Transform::identity());

    EXPECT_EQ(dst.vertices().size(), 4u);
    EXPECT_EQ(dst.edges().size(), 4u);
    EXPECT_EQ(dst.faces().size(), 0u);
}

// Naming faces in the set is the ONLY way a face comes back: copyInto
// re-states each via addFaceOnLoop with its transformed source normal, so
// a boxed group round-trips with all 6 faces intact and correctly wound.
TEST(SceneOpsTest, CopyIntoWithFacesInTheSetRecreatesAllSixBoxFaces) {
    Model src;
    const Id faceId = buildRectangleFace(src);
    ASSERT_NE(faceId, kInvalidId);
    const ExtrudeResult extrude = src.extrudeFace(faceId, 2.0);
    ASSERT_TRUE(extrude.ok);
    ASSERT_EQ(src.faces().size(), 6u);

    std::vector<std::pair<EntityKind, Id>> seeds;
    for (const auto& [id, face] : src.faces()) {
        (void)face;
        seeds.emplace_back(EntityKind::Face, id);
    }
    const EntitySet set = closureOf(src, seeds);
    ASSERT_EQ(set.faces.size(), 6u);
    ASSERT_EQ(set.edges.size(), 12u);

    Model dst;
    copyInto(dst, src, set, Transform::identity());

    EXPECT_EQ(dst.vertices().size(), 8u);
    EXPECT_EQ(dst.edges().size(), 12u);
    EXPECT_EQ(dst.faces().size(), 6u);
}

// Regression pin: copyInto of a closed, outward-oriented box with an
// identity transform must produce a POSITIVE-volume solid, not an
// inside-out shell -- see the maintainer notes.
TEST(SceneOpsTest, CopyIntoOfAClosedOutwardBoxProducesAPositiveVolumeSolidInAnEmptyDestination) {
    Model src;
    makeOutwardBox(src, Vec3{0.0, 0.0, 0.0}, Vec3{2.0, 3.0, 4.0});
    ASSERT_TRUE(isSolid(src).solid);
    const double srcVolume = solidVolume(src);
    ASSERT_NEAR(srcVolume, 2.0 * 3.0 * 4.0, 1e-9);

    const EntitySet set = closeWholeModel(src);
    ASSERT_EQ(set.faces.size(), 6u);
    ASSERT_EQ(set.edges.size(), 12u);

    Model dst;
    copyInto(dst, src, set, Transform::identity());

    EXPECT_EQ(dst.faces().size(), 6u);
    const auto info = isSolid(dst);
    EXPECT_TRUE(info.solid);
    EXPECT_FALSE(info.nonPositiveVolume);
    EXPECT_GT(solidVolume(dst), 0.0);
    EXPECT_NEAR(solidVolume(dst), srcVolume, 1e-9);
}

// Same guarantee under a non-identity transform (the ordinary Make Group/
// array-copy case: a translated, not-in-place, destination) -- confirms the
// fix isn't accidentally position-dependent.
TEST(SceneOpsTest, CopyIntoOfAClosedOutwardBoxWithATranslationStillProducesAPositiveVolumeSolid) {
    Model src;
    makeOutwardBox(src, Vec3{0.0, 0.0, 0.0}, Vec3{2.0, 3.0, 4.0});
    const double srcVolume = solidVolume(src);

    const EntitySet set = closeWholeModel(src);

    Model dst;
    copyInto(dst, src, set, Transform::translation(Vec3{10.0, -5.0, 100.0}));

    const auto info = isSolid(dst);
    EXPECT_TRUE(info.solid);
    EXPECT_GT(solidVolume(dst), 0.0);
    EXPECT_NEAR(solidVolume(dst), srcVolume, 1e-9);
}

// Mirrors makeGroup's copyInto (box -> Definition) followed by explode's
// copyInto (Definition -> fresh model) -- both legs must preserve winding
// or the round-trip silently leaves the geometry inside-out.
TEST(SceneOpsTest, CopyIntoRoundTripThroughAGroupAndBackStaysAPositiveVolumeSolid) {
    Model src;
    makeOutwardBox(src, Vec3{0.0, 0.0, 0.0}, Vec3{2.0, 3.0, 4.0});
    const double srcVolume = solidVolume(src);

    Model group;
    copyInto(group, src, closeWholeModel(src), Transform::identity());
    ASSERT_TRUE(isSolid(group).solid);
    ASSERT_NEAR(solidVolume(group), srcVolume, 1e-9);

    Model exploded;
    copyInto(exploded, group, closeWholeModel(group), Transform::identity());

    const auto info = isSolid(exploded);
    EXPECT_TRUE(info.solid);
    EXPECT_NEAR(solidVolume(exploded), srcVolume, 1e-9);
}

// Self-copy (&dst == &src) idempotence case: the face named in `set`
// already exists in dst before this call starts, so faceExistsOnLoop must
// catch it, or addFaceOnLoop would stack a duplicate, inverted-normal face.
TEST(SceneOpsTest, CopyIntoSelfCopyWithIdentityTransformDoesNotDuplicateTheFace) {
    Model model;
    const Id faceId = buildRectangleFace(model);
    ASSERT_NE(faceId, kInvalidId);
    const EntitySet set = closureOf(model, {{EntityKind::Face, faceId}});

    copyInto(model, model, set, Transform::identity());

    EXPECT_EQ(model.vertices().size(), 4u);
    EXPECT_EQ(model.edges().size(), 4u);
    EXPECT_EQ(model.faces().size(), 1u);  // still just the one original face -- no duplicate stacked on top
    EXPECT_NE(model.face(faceId), nullptr);
}

TEST(SceneOpsTest, CopyIntoDropsAnIsolatedSelectedVertexWithNoEdgeInTheSet) {
    Model src;
    const auto v = src.addEdge({0.0, 0.0, 0.0}, {1.0, 0.0, 0.0});
    ASSERT_TRUE(v.created);
    const auto lonely = src.addEdge({5.0, 0.0, 0.0}, {6.0, 0.0, 0.0});
    ASSERT_TRUE(lonely.created);

    // Only v's edge is in the set -- lonely's vertices are never referenced.
    EntitySet set;
    set.edges.push_back(v.edge);

    Model dst;
    copyInto(dst, src, set, Transform::identity());

    EXPECT_EQ(dst.edges().size(), 1u);
    EXPECT_EQ(dst.vertices().size(), 2u);  // only v's endpoints -- lonely's are dropped
}

TEST(SceneOpsTest, RemoveFromDeletesTheClosuresEdgesAndItsFace) {
    Model model;
    const Id faceId = buildRectangleFace(model);
    ASSERT_NE(faceId, kInvalidId);
    const EntitySet set = closureOf(model, {{EntityKind::Face, faceId}});
    ASSERT_EQ(set.edges.size(), 4u);

    removeFrom(model, set);

    EXPECT_TRUE(model.edges().empty());
    EXPECT_TRUE(model.faces().empty());
    EXPECT_TRUE(model.vertices().empty());  // nothing else kept these vertices alive
}

TEST(SceneOpsTest, RemoveFromDissolvesANeighborFaceThatSharesARemovedEdge) {
    Model model;
    // Rectangle A: p0(0,0)-p1(4,0)-p2(4,3)-p3(0,3), closing back to p0.
    const Vec3 p0{0.0, 0.0, 0.0};
    const Vec3 p1{4.0, 0.0, 0.0};
    const Vec3 p2{4.0, 3.0, 0.0};
    const Vec3 p3{0.0, 3.0, 0.0};
    model.addEdge(p0, p1);
    model.addEdge(p1, p2);
    model.addEdge(p2, p3);
    const auto closingA = model.addEdge(p3, p0);
    ASSERT_EQ(closingA.newFaces.size(), 1u);
    const Id faceA = closingA.newFaces[0];

    // Rectangle B, sharing edge p1-p2 with A. That shared edge is added
    // FIRST (resolves to the already-existing edge, no loop detection); the
    // closing edge added last triggers face detection for B via the twin side.
    const Vec3 p4{8.0, 0.0, 0.0};
    const Vec3 p5{8.0, 3.0, 0.0};
    model.addEdge(p2, p1);
    model.addEdge(p1, p4);
    model.addEdge(p4, p5);
    const auto closingB = model.addEdge(p5, p2);
    ASSERT_EQ(closingB.newFaces.size(), 1u);
    const Id faceB = closingB.newFaces[0];
    ASSERT_EQ(model.faces().size(), 2u);
    ASSERT_EQ(model.edges().size(), 7u);  // 4 (A) + 3 new (B) -- the shared edge isn't duplicated

    const EntitySet closure = closureOf(model, {{EntityKind::Face, faceA}});
    ASSERT_EQ(closure.edges.size(), 4u);

    removeFrom(model, closure);

    // Both faces are gone: A directly, B as the documented MVP side effect
    // -- B shared the removed edge, and removeEdge dissolves any face that
    // references it, not only ones in the caller's set (see removeFrom's header).
    EXPECT_EQ(model.faces().count(faceA), 0u);
    EXPECT_EQ(model.faces().count(faceB), 0u);
    EXPECT_TRUE(model.faces().empty());
    EXPECT_EQ(model.edges().size(), 3u);  // B's 3 non-shared edges remain as wire edges
}

}  // namespace
