#include <geo/model.h>

#include <algorithm>
#include <tuple>
#include <vector>

#include <gtest/gtest.h>

namespace {

using plnr::geo::AddEdgeResult;
using plnr::geo::Face;
using plnr::geo::Id;
using plnr::geo::kInvalidId;
using plnr::geo::kMergeTol;
using plnr::geo::Model;
using plnr::geo::SplitEdgeResult;
using plnr::geo::SplitFaceResult;
using plnr::geo::Vec3;

using SizeSnapshot = std::tuple<std::size_t, std::size_t, std::size_t, std::size_t>;

// vertices/edges/half-edges/faces -- same full-record-count comparison
// pattern as tests/geo/extrude_test.cpp's own snapshot().
SizeSnapshot snapshot(const Model& model) {
    return {model.vertices().size(), model.edges().size(), model.halfEdges().size(), model.faces().size()};
}

// Walks a face's half-edge next-cycle from Face::halfEdge and asserts it
// returns to the start after loop.size() steps -- stronger than
// faceVertexLoop() alone; also catches a mismatched next/prev cycle length.
void expectValidNextCycle(const Model& model, Id faceId) {
    const Face* face = model.face(faceId);
    ASSERT_NE(face, nullptr);
    const std::vector<Id> loop = model.faceVertexLoop(faceId);
    ASSERT_GE(loop.size(), 3u);

    Id cur = face->halfEdge;
    for (std::size_t i = 0; i < loop.size(); ++i) {
        const auto* he = model.halfEdge(cur);
        ASSERT_NE(he, nullptr);
        EXPECT_EQ(he->face, faceId);
        cur = he->next;
    }
    EXPECT_EQ(cur, face->halfEdge) << "next-cycle did not return to face's own start half-edge";
}

// --- Wire edge -------------------------------------------------------------

TEST(SplitEdgeTest, WireEdgeSplitYieldsTwoEdgesThreeVerticesNoFaces) {
    Model model;
    const auto added = model.addEdge(Vec3{0.0, 0.0, 0.0}, Vec3{2.0, 0.0, 0.0});
    ASSERT_TRUE(added.created);

    const SplitEdgeResult result = model.splitEdge(added.edge, Vec3{1.0, 0.0, 0.0});
    ASSERT_TRUE(result.ok);
    ASSERT_NE(result.newVertex, kInvalidId);
    ASSERT_NE(result.edgeA, kInvalidId);
    ASSERT_NE(result.edgeB, kInvalidId);

    EXPECT_EQ(model.vertices().size(), 3u);
    EXPECT_EQ(model.edges().size(), 2u);
    EXPECT_EQ(model.halfEdges().size(), 4u);
    EXPECT_TRUE(model.faces().empty());

    ASSERT_NE(model.vertex(result.newVertex), nullptr);
    EXPECT_NEAR(model.vertex(result.newVertex)->pos.x, 1.0, 1e-9);

    // Old edge id no longer resolves; the two new ones do, and both halves
    // stay wire (face == kInvalidId).
    EXPECT_EQ(model.edge(added.edge), nullptr);
    const auto* edgeA = model.edge(result.edgeA);
    const auto* edgeB = model.edge(result.edgeB);
    ASSERT_NE(edgeA, nullptr);
    ASSERT_NE(edgeB, nullptr);
    for (Id heId : edgeA->halfEdges) {
        EXPECT_EQ(model.halfEdge(heId)->face, kInvalidId);
    }
    for (Id heId : edgeB->halfEdges) {
        EXPECT_EQ(model.halfEdge(heId)->face, kInvalidId);
    }
}

// --- Face edge (single face) ------------------------------------------------

TEST(SplitEdgeTest, TriangleFaceEdgeSplitSurvivesWithSameIdAndFourVertexLoop) {
    Model model;
    const Vec3 a{0.0, 0.0, 0.0};
    const Vec3 b{2.0, 0.0, 0.0};
    const Vec3 c{0.0, 2.0, 0.0};

    const AddEdgeResult ab = model.addEdge(a, b);
    model.addEdge(b, c);
    const AddEdgeResult closing = model.addEdge(c, a);
    ASSERT_TRUE(ab.created);
    ASSERT_TRUE(closing.created);
    ASSERT_EQ(closing.newFaces.size(), 1u);
    const Id faceId = closing.newFaces[0];
    const Vec3 normalBefore = model.face(faceId)->normal;

    // Split edge a-b at its midpoint (1, 0, 0).
    const Vec3 midAB{1.0, 0.0, 0.0};
    const SplitEdgeResult result = model.splitEdge(ab.edge, midAB);
    ASSERT_TRUE(result.ok);

    // Same face id survives -- no dissolve/recreate.
    ASSERT_NE(model.face(faceId), nullptr);
    EXPECT_EQ(model.faces().size(), 1u);

    const std::vector<Id> loop = model.faceVertexLoop(faceId);
    ASSERT_EQ(loop.size(), 4u);
    // Original winding a -> b -> c -> a, now with M spliced in between a and
    // b: a -> M -> b -> c -> a.
    EXPECT_EQ(model.vertex(loop[0])->pos.x, a.x);
    EXPECT_NEAR(model.vertex(loop[1])->pos.x, midAB.x, 1e-9);
    EXPECT_EQ(loop[1], result.newVertex);
    EXPECT_NEAR(model.vertex(loop[2])->pos.x, b.x, 1e-9);
    EXPECT_NEAR(model.vertex(loop[3])->pos.x, c.x, 1e-9);

    // Normal is untouched (M lies exactly on the original planar segment).
    EXPECT_NEAR(model.face(faceId)->normal.x, normalBefore.x, 1e-9);
    EXPECT_NEAR(model.face(faceId)->normal.y, normalBefore.y, 1e-9);
    EXPECT_NEAR(model.face(faceId)->normal.z, normalBefore.z, 1e-9);

    expectValidNextCycle(model, faceId);
}

// --- Edge shared by two faces ------------------------------------------------

// Unit square split into two triangles by diagonal a-c via addEdge's own
// auto-face-detection: face1=a-b-c, face2=d-a-c, sharing the diagonal edge
// with opposite half-edge directions -- splitEdge must re-chain each side.
struct SharedDiagonalFixture {
    Model model;
    Id faceAbc{kInvalidId};
    Id faceDac{kInvalidId};
    Id diagonalEdge{kInvalidId};
    Vec3 a{0.0, 0.0, 0.0};
    Vec3 b{1.0, 0.0, 0.0};
    Vec3 c{1.0, 1.0, 0.0};
    Vec3 d{0.0, 1.0, 0.0};
};

SharedDiagonalFixture buildSharedDiagonalFixture() {
    SharedDiagonalFixture f;
    f.model.addEdge(f.a, f.b);
    f.model.addEdge(f.b, f.c);
    const AddEdgeResult diag = f.model.addEdge(f.c, f.a);
    EXPECT_TRUE(diag.created);
    EXPECT_EQ(diag.newFaces.size(), 1u);
    f.faceAbc = diag.newFaces[0];
    f.diagonalEdge = diag.edge;

    f.model.addEdge(f.c, f.d);
    const AddEdgeResult closing = f.model.addEdge(f.d, f.a);
    EXPECT_TRUE(closing.created);
    EXPECT_EQ(closing.newFaces.size(), 1u);
    f.faceDac = closing.newFaces[0];

    EXPECT_EQ(f.model.faces().size(), 2u);
    return f;
}

TEST(SplitEdgeTest, SharedEdgeSplitBothFacesSurviveAndGainMidpointVertex) {
    SharedDiagonalFixture f = buildSharedDiagonalFixture();
    ASSERT_NE(f.faceAbc, f.faceDac);

    const Vec3 midDiagonal{0.5, 0.5, 0.0};
    const SplitEdgeResult result = f.model.splitEdge(f.diagonalEdge, midDiagonal);
    ASSERT_TRUE(result.ok);

    // Both original face ids survive.
    EXPECT_EQ(f.model.faces().size(), 2u);
    ASSERT_NE(f.model.face(f.faceAbc), nullptr);
    ASSERT_NE(f.model.face(f.faceDac), nullptr);

    const std::vector<Id> loopAbc = f.model.faceVertexLoop(f.faceAbc);
    const std::vector<Id> loopDac = f.model.faceVertexLoop(f.faceDac);
    ASSERT_EQ(loopAbc.size(), 4u);
    ASSERT_EQ(loopDac.size(), 4u);

    // Both loops contain the new midpoint vertex, in the right cyclic spot:
    // a -> b -> c -> M -> a, and d -> a -> M -> c -> d.
    const auto containsInOrder = [](const std::vector<Id>& loop, const std::vector<Id>& expectedCycle) {
        // Rotate loop to start at expectedCycle[0], then compare.
        const auto it = std::find(loop.begin(), loop.end(), expectedCycle[0]);
        if (it == loop.end()) return false;
        std::vector<Id> rotated(it, loop.end());
        rotated.insert(rotated.end(), loop.begin(), it);
        return rotated == expectedCycle;
    };

    const Id va = f.model.findVertex(f.a)->id;
    const Id vb = f.model.findVertex(f.b)->id;
    const Id vc = f.model.findVertex(f.c)->id;
    const Id vd = f.model.findVertex(f.d)->id;
    const Id vm = result.newVertex;

    EXPECT_TRUE(containsInOrder(loopAbc, {va, vb, vc, vm}));
    EXPECT_TRUE(containsInOrder(loopDac, {vd, va, vm, vc}));

    expectValidNextCycle(f.model, f.faceAbc);
    expectValidNextCycle(f.model, f.faceDac);

    // Unrelated ids (the square's outer boundary vertices/edges/faces) are
    // untouched.
    EXPECT_EQ(f.model.vertex(vb)->pos.x, f.b.x);
    EXPECT_EQ(f.model.vertex(vd)->pos.y, f.d.y);
}

// --- Guards ------------------------------------------------------------------

TEST(SplitEdgeTest, UnknownEdgeIdIsRejectedAndModelUntouched) {
    Model model;
    model.addEdge(Vec3{0.0, 0.0, 0.0}, Vec3{1.0, 0.0, 0.0});
    const SizeSnapshot before = snapshot(model);

    const SplitEdgeResult result = model.splitEdge(999999, Vec3{0.5, 0.0, 0.0});
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(snapshot(model), before);
}

TEST(SplitEdgeTest, EndpointAdjacentPointIsRejectedAndModelUntouched) {
    Model model;
    const auto added = model.addEdge(Vec3{0.0, 0.0, 0.0}, Vec3{2.0, 0.0, 0.0});
    const SizeSnapshot before = snapshot(model);

    const Vec3 nearA{kMergeTol * 0.5, 0.0, 0.0};
    EXPECT_FALSE(model.splitEdge(added.edge, nearA).ok);
    EXPECT_EQ(snapshot(model), before);

    const Vec3 nearB{2.0 - kMergeTol * 0.5, 0.0, 0.0};
    EXPECT_FALSE(model.splitEdge(added.edge, nearB).ok);
    EXPECT_EQ(snapshot(model), before);
}

TEST(SplitEdgeTest, OffSegmentPointIsRejectedAndModelUntouched) {
    Model model;
    const auto added = model.addEdge(Vec3{0.0, 0.0, 0.0}, Vec3{2.0, 0.0, 0.0});
    const SizeSnapshot before = snapshot(model);

    // Projects (clamped) to (1, 0, 0) on the segment, but 1 unit away
    // perpendicular -- far beyond kMergeTol.
    const Vec3 offSegment{1.0, 1.0, 0.0};
    EXPECT_FALSE(model.splitEdge(added.edge, offSegment).ok);
    EXPECT_EQ(snapshot(model), before);
}

// --- Twin/outgoing invariants -----------------------------------------------

TEST(SplitEdgeTest, TwinAndOutgoingInvariantsHoldAfterSplit) {
    Model model;
    const Vec3 a{0.0, 0.0, 0.0};
    const Vec3 b{2.0, 0.0, 0.0};
    const Vec3 c{0.0, 2.0, 0.0};
    const AddEdgeResult ab = model.addEdge(a, b);
    model.addEdge(b, c);
    const AddEdgeResult closing = model.addEdge(c, a);
    const Id faceId = closing.newFaces[0];

    const SplitEdgeResult result = model.splitEdge(ab.edge, Vec3{1.0, 0.0, 0.0});
    ASSERT_TRUE(result.ok);

    const auto* edgeA = model.edge(result.edgeA);
    const auto* edgeB = model.edge(result.edgeB);
    ASSERT_NE(edgeA, nullptr);
    ASSERT_NE(edgeB, nullptr);

    // Every half-edge's twin resolves and points back.
    for (Id heId : {edgeA->halfEdges[0], edgeA->halfEdges[1], edgeB->halfEdges[0], edgeB->halfEdges[1]}) {
        const auto* he = model.halfEdge(heId);
        ASSERT_NE(he, nullptr);
        const auto* twin = model.halfEdge(he->twin);
        ASSERT_NE(twin, nullptr);
        EXPECT_EQ(twin->twin, heId);
    }

    // M has exactly two outgoing half-edges (h0b, h1a), both resolvable.
    const auto* m = model.vertex(result.newVertex);
    ASSERT_NE(m, nullptr);
    EXPECT_EQ(m->outgoing.size(), 2u);
    for (Id heId : m->outgoing) {
        EXPECT_NE(model.halfEdge(heId), nullptr);
    }

    // A and B each still have exactly the same outgoing count as before the
    // split (one outgoing half-edge replaced in place, not added/removed).
    const Id va = model.findVertex(a)->id;
    const Id vb = model.findVertex(b)->id;
    EXPECT_EQ(model.vertex(va)->outgoing.size(), 2u);  // a->M and a->c (triangle)
    EXPECT_EQ(model.vertex(vb)->outgoing.size(), 2u);  // b->M and b->c

    expectValidNextCycle(model, faceId);
}

// --- Vertex-reuse (weld) refinement -----------------------------------------

// The splitting pass welds a crossing by splitting BOTH edges at the SAME
// point -- guarantees the weld lands on one shared vertex, not two
// coincident ones. See the maintainer notes.
TEST(SplitEdgeTest, SplittingTwoEdgesAtTheSameCrossingPointReusesTheSharedVertex) {
    Model model;
    const AddEdgeResult e1 = model.addEdge(Vec3{-1.0, 0.0, 0.0}, Vec3{1.0, 0.0, 0.0});
    const AddEdgeResult e2 = model.addEdge(Vec3{0.0, -1.0, 0.0}, Vec3{0.0, 1.0, 0.0});
    ASSERT_TRUE(e1.created);
    ASSERT_TRUE(e2.created);
    ASSERT_EQ(model.vertices().size(), 4u);

    const Vec3 crossing{0.0, 0.0, 0.0};
    const SplitEdgeResult split1 = model.splitEdge(e1.edge, crossing);
    ASSERT_TRUE(split1.ok);
    EXPECT_EQ(model.vertices().size(), 5u);  // 4 original endpoints + 1 new crossing vertex

    const SplitEdgeResult split2 = model.splitEdge(e2.edge, crossing);
    ASSERT_TRUE(split2.ok);

    // The weld: split2's own new vertex is the SAME one split1 just created,
    // not a second coincident one.
    EXPECT_EQ(split2.newVertex, split1.newVertex);
    EXPECT_EQ(model.vertices().size(), 5u);  // still 5, not 6
    EXPECT_EQ(model.edges().size(), 4u);     // each original edge split into 2

    const auto* center = model.vertex(split1.newVertex);
    ASSERT_NE(center, nullptr);
    EXPECT_EQ(center->outgoing.size(), 4u);  // all 4 fragment half-edges touch here
}

// --- splitFaceByChord --------------------------------------------------------

TEST(SplitFaceByChordTest, ChordThroughARectangleProducesTwoTrianglesSharingTheChord) {
    Model model;
    const Vec3 a{0.0, 0.0, 0.0};
    const Vec3 b{4.0, 0.0, 0.0};
    const Vec3 c{4.0, 3.0, 0.0};
    const Vec3 d{0.0, 3.0, 0.0};
    model.addEdge(a, b);
    model.addEdge(b, c);
    model.addEdge(c, d);
    const AddEdgeResult closing = model.addEdge(d, a);
    ASSERT_EQ(closing.newFaces.size(), 1u);
    const Id faceId = closing.newFaces[0];

    // detectFaces=false: a plain wire diagonal, exactly what the splitting
    // pass hands to splitFaceByChord (see the guard test below for the
    // auto-detection collision case).
    const AddEdgeResult chord = model.addEdge(a, c, /*detectFaces=*/false);
    ASSERT_TRUE(chord.created);
    ASSERT_TRUE(chord.newFaces.empty());
    ASSERT_EQ(model.faces().size(), 1u);

    const SplitFaceResult result = model.splitFaceByChord(faceId, chord.edge);
    ASSERT_TRUE(result.ok);
    ASSERT_NE(result.faceA, kInvalidId);
    ASSERT_NE(result.faceB, kInvalidId);
    EXPECT_NE(result.faceA, result.faceB);

    EXPECT_EQ(model.face(faceId), nullptr);  // dissolved
    EXPECT_EQ(model.faces().size(), 2u);

    const Id va = model.findVertex(a)->id;
    const Id vb = model.findVertex(b)->id;
    const Id vc = model.findVertex(c)->id;
    const Id vd = model.findVertex(d)->id;

    const std::vector<Id> loopA = model.faceVertexLoop(result.faceA);
    const std::vector<Id> loopB = model.faceVertexLoop(result.faceB);
    ASSERT_EQ(loopA.size(), 3u);
    ASSERT_EQ(loopB.size(), 3u);

    const auto contains = [](const std::vector<Id>& loop, Id id) {
        return std::find(loop.begin(), loop.end(), id) != loop.end();
    };
    // faceA = a-b-c, faceB = c-d-a -- both contain the chord's own endpoints.
    EXPECT_TRUE(contains(loopA, va));
    EXPECT_TRUE(contains(loopA, vb));
    EXPECT_TRUE(contains(loopA, vc));
    EXPECT_TRUE(contains(loopB, vc));
    EXPECT_TRUE(contains(loopB, vd));
    EXPECT_TRUE(contains(loopB, va));

    expectValidNextCycle(model, result.faceA);
    expectValidNextCycle(model, result.faceB);
}

TEST(SplitFaceByChordTest, ChordAlreadyClaimedByAnotherFaceIsRejectedAndModelUntouched) {
    Model model;
    const Vec3 a{0.0, 0.0, 0.0};
    const Vec3 b{4.0, 0.0, 0.0};
    const Vec3 c{4.0, 3.0, 0.0};
    const Vec3 d{0.0, 3.0, 0.0};
    model.addEdge(a, b);
    model.addEdge(b, c);
    model.addEdge(c, d);
    const AddEdgeResult closing = model.addEdge(d, a);
    const Id faceId = closing.newFaces[0];

    // detectFaces=true here: addEdge's own auto-detection claims the a-b-c
    // triangle before splitFaceByChord runs, so the chord isn't wire on both
    // sides anymore
    const AddEdgeResult chord = model.addEdge(a, c);
    ASSERT_TRUE(chord.created);
    ASSERT_EQ(chord.newFaces.size(), 1u);  // the auto-detected a-b-c triangle
    ASSERT_EQ(model.faces().size(), 2u);   // original quad + auto triangle, coexisting

    const SizeSnapshot before = snapshot(model);
    const SplitFaceResult result = model.splitFaceByChord(faceId, chord.edge);
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(snapshot(model), before);
}

TEST(SplitFaceByChordTest, ChordEndpointsNotOnTheFaceLoopIsRejectedAndModelUntouched) {
    Model model;
    const Vec3 a{0.0, 0.0, 0.0};
    const Vec3 b{4.0, 0.0, 0.0};
    const Vec3 c{4.0, 3.0, 0.0};
    const Vec3 d{0.0, 3.0, 0.0};
    model.addEdge(a, b);
    model.addEdge(b, c);
    model.addEdge(c, d);
    const AddEdgeResult closing = model.addEdge(d, a);
    const Id faceId = closing.newFaces[0];

    // A wire edge entirely off the face -- neither endpoint is on faceId's
    // own vertex loop.
    const AddEdgeResult stray = model.addEdge(Vec3{10.0, 10.0, 0.0}, Vec3{11.0, 11.0, 0.0});
    const SizeSnapshot before = snapshot(model);

    const SplitFaceResult result = model.splitFaceByChord(faceId, stray.edge);
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(snapshot(model), before);
}

TEST(SplitFaceByChordTest, UnknownFaceOrEdgeIdIsRejectedAndModelUntouched) {
    Model model;
    const Vec3 a{0.0, 0.0, 0.0};
    const Vec3 b{4.0, 0.0, 0.0};
    const Vec3 c{4.0, 3.0, 0.0};
    const Vec3 d{0.0, 3.0, 0.0};
    model.addEdge(a, b);
    model.addEdge(b, c);
    model.addEdge(c, d);
    const AddEdgeResult closing = model.addEdge(d, a);
    const Id faceId = closing.newFaces[0];
    const AddEdgeResult chord = model.addEdge(a, c, /*detectFaces=*/false);

    const SizeSnapshot before = snapshot(model);
    EXPECT_FALSE(model.splitFaceByChord(999999, chord.edge).ok);
    EXPECT_FALSE(model.splitFaceByChord(faceId, 999999).ok);
    EXPECT_EQ(snapshot(model), before);
}

}  // namespace
