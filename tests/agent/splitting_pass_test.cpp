#include "agent/geometry_api.h"

#include <algorithm>
#include <memory>
#include <vector>

#include <ordo/core/kernel.h>

#include "agent/events.h"
#include "agent/command/geometry_commands.h"

#include <geo/entity.h>
#include <geo/shapes.h>

#include <gtest/gtest.h>

namespace {

using ordo::core::Kernel;
using plnr::agent::AddEdgeCommand;
using plnr::agent::AddPolylineCommand;
using plnr::agent::AddRectangleCommand;
using plnr::agent::FollowMeCommand;
using plnr::agent::GeometryApi;
using plnr::agent::kGeometryApiName;
using plnr::events::AddEdgeRequested;
using plnr::events::AddPolylineRequested;
using plnr::events::AddRectangleRequested;
using plnr::events::FollowMeRequested;
using plnr::events::GeometryChanged;
using plnr::geo::AddEdgeResult;
using plnr::geo::Id;
using plnr::geo::Vec3;
using plnr::geo::Vertex;

// Exercises GeometryApi::runSplittingPass through addEdge/addRectangle/
// addPolyline plus divideEdge -- a fresh kernel with drawing commands
// registered and a GeometryChanged counter, no Presenter.
class SplittingPassTest : public ::testing::Test {
protected:
    void SetUp() override {
        kernel.registerAgent(std::make_shared<GeometryApi>());
        kernel.registerCommand<AddEdgeRequested, AddEdgeCommand>();
        kernel.registerCommand<AddRectangleRequested, AddRectangleCommand>();
        kernel.registerCommand<AddPolylineRequested, AddPolylineCommand>();
        kernel.registerCommand<FollowMeRequested, FollowMeCommand>();
        kernel.dispatcher().subscribe<GeometryChanged>(&changedCount,
                                                         [this](const GeometryChanged&) { ++changedCount; });
        agent = kernel.agentAs<GeometryApi>(kGeometryApiName);
    }

    Kernel kernel;
    std::shared_ptr<GeometryApi> agent;
    int changedCount = 0;
};

// --- (a-2) edge-cross splits, new vs existing ---

TEST_F(SplittingPassTest, CrossingLinesInEmptySpaceBothSplitWithAWeldedCenterVertex) {
    // First line: nothing to cross yet -- its own splitting pass is a no-op.
    kernel.send(AddEdgeRequested{{-2, 0, 0}, {2, 0, 0}});
    ASSERT_EQ(agent->model().edges().size(), 1u);
    ASSERT_EQ(changedCount, 1);

    // Second line crosses the first at the origin -- a free-floating
    // crossing splits regardless of face membership. The crossing vertex is
    // WELDED (one shared vertex, not two coincident ones).
    kernel.send(AddEdgeRequested{{0, -2, 0}, {0, 2, 0}});

    EXPECT_EQ(changedCount, 2);  // still ONE event for the whole second draw + its splits
    EXPECT_EQ(agent->model().edges().size(), 4u);
    EXPECT_EQ(agent->model().vertices().size(), 5u);  // 4 original endpoints + 1 welded center

    const Vertex* center = agent->model().findVertex({0, 0, 0});
    ASSERT_NE(center, nullptr);
    EXPECT_EQ(center->outgoing.size(), 4u);  // all 4 fragment half-edges meet here
}

// --- (a-1) endpoint-lands-on-existing-edge + (b) face-chord split ----------

TEST_F(SplittingPassTest, LineAcrossAFaceInteriorSplitsBothBoundaryEdgesAndTheFaceItself) {
    // Rectangle (0,0)-(4,3): 4 edges, 1 face.
    kernel.send(AddRectangleRequested{{0, 0, 0}, {4, 3, 0}});
    ASSERT_EQ(agent->model().faces().size(), 1u);
    const Id originalFace = agent->model().faces().begin()->first;
    ASSERT_EQ(changedCount, 1);

    // Both new-edge endpoints sit ON an existing boundary edge's interior (a
    // "T" landing, not a crossing geo::segmentIntersect would catch) --
    // exactly what phase (a-1) exists to catch.
    kernel.send(AddEdgeRequested{{2, 0, 0}, {2, 3, 0}});

    EXPECT_EQ(changedCount, 2);  // one more event for the whole draw + its splits
    EXPECT_EQ(agent->model().faces().size(), 2u);
    EXPECT_EQ(agent->model().face(originalFace), nullptr);  // dissolved by the chord split

    // 4 original boundary edges - 2 split ones + 2*2 fragments + 1 chord = 7.
    EXPECT_EQ(agent->model().edges().size(), 7u);
    // 4 corners + 2 new split points (welded onto the chord's own endpoints,
    // not duplicated -- see splitEdge's vertex-reuse refinement).
    EXPECT_EQ(agent->model().vertices().size(), 6u);

    const Vertex* m1 = agent->model().findVertex({2, 0, 0});
    const Vertex* m2 = agent->model().findVertex({2, 3, 0});
    ASSERT_NE(m1, nullptr);
    ASSERT_NE(m2, nullptr);

    // Both faces' own loops contain the chord's two endpoints.
    for (const auto& [faceId, face] : agent->model().faces()) {
        (void)face;
        const std::vector<Id> loop = agent->model().faceVertexLoop(faceId);
        EXPECT_NE(std::find(loop.begin(), loop.end(), m1->id), loop.end());
        EXPECT_NE(std::find(loop.begin(), loop.end(), m2->id), loop.end());
    }
}

// --- addRectangle's own path also runs the pass -----------------------------

TEST_F(SplittingPassTest, RectangleDrawnOverlappingAnExistingEdgeSplitsThroughAddRectanglesPath) {
    // Pre-existing vertical edge that will pass straight through the
    // rectangle's bottom and top sides once it's drawn.
    kernel.send(AddEdgeRequested{{3, -1, 0}, {3, 4, 0}});
    ASSERT_EQ(agent->model().edges().size(), 1u);
    const Id preExistingEdge = agent->model().edges().begin()->first;
    ASSERT_EQ(changedCount, 1);

    kernel.send(AddRectangleRequested{{0, 0, 0}, {4, 3, 0}});

    EXPECT_EQ(changedCount, 2);  // one event for the whole rectangle draw + its splits
    // Pre-existing edge crosses BOTH the bottom and top sides -> splits into
    // 3 fragments (net +2). Bottom and top each split once (net +1 each).
    // Right/left sides are untouched. 1 (pre-existing) + 4 (rect) + 2 + 1 + 1 = 9.
    EXPECT_EQ(agent->model().edges().size(), 9u);
    // 2 (pre-existing endpoints) + 4 (corners) + 2 (new, welded crossing points) = 8.
    EXPECT_EQ(agent->model().vertices().size(), 8u);
    // The rectangle's face still exists -- neither crossing fragment
    // qualifies as a face-chord (they're the face's own boundary edges,
    // rejected by splitFaceByChord's adjacent-in-loop guard).
    EXPECT_EQ(agent->model().faces().size(), 1u);
    EXPECT_EQ(agent->model().edge(preExistingEdge), nullptr);  // replaced by its own fragments

    const Vertex* crossLow = agent->model().findVertex({3, 0, 0});
    const Vertex* crossHigh = agent->model().findVertex({3, 3, 0});
    ASSERT_NE(crossLow, nullptr);
    ASSERT_NE(crossHigh, nullptr);
    EXPECT_EQ(crossLow->outgoing.size(), 4u);
    EXPECT_EQ(crossHigh->outgoing.size(), 4u);
}

// --- (a-3) edge-cross splits, new vs new ------------------------------------

TEST_F(SplittingPassTest, SelfCrossingPolylineSplitsAtItsOwnCrossingWithAWeldedVertex) {
    // Bowtie polyline: segments 1 (0,0)-(2,2) and 3 (2,0)-(0,2) cross at
    // (1,1); segment 2 only touches each at a shared endpoint. Phase (a-2)
    // excludes new-vs-new pairs, so only phase (a-3) catches this.
    const std::vector<Vec3> points = {{0, 0, 0}, {2, 2, 0}, {2, 0, 0}, {0, 2, 0}};
    const bool changed = agent->addPolyline(points, false);

    EXPECT_TRUE(changed);
    EXPECT_EQ(changedCount, 1);  // still ONE event for the whole polyline + its self-crossing split
    EXPECT_EQ(agent->model().vertices().size(), 5u);  // 4 original + 1 welded self-crossing vertex
    EXPECT_EQ(agent->model().edges().size(), 5u);     // 3 segments, 2 of which split once each

    const Vertex* crossing = agent->model().findVertex({1, 1, 0});
    ASSERT_NE(crossing, nullptr);
    EXPECT_EQ(crossing->outgoing.size(), 4u);  // both crossing segments contribute 2 each
}

// --- divideEdge --------------------------------------------------------------

TEST_F(SplittingPassTest, DivideEdgeIntoFourEqualSegmentsProducesFourCollinearEdgesAndOneEvent) {
    kernel.send(AddEdgeRequested{{0, 0, 0}, {8, 0, 0}});
    ASSERT_EQ(changedCount, 1);
    const Id edgeId = agent->model().edges().begin()->first;

    const bool divided = agent->divideEdge(edgeId, 4);

    EXPECT_TRUE(divided);
    EXPECT_EQ(changedCount, 2);  // one more event for the whole divide
    EXPECT_EQ(agent->model().edges().size(), 4u);
    EXPECT_EQ(agent->model().vertices().size(), 5u);

    EXPECT_NE(agent->model().findVertex({2, 0, 0}), nullptr);
    EXPECT_NE(agent->model().findVertex({4, 0, 0}), nullptr);
    EXPECT_NE(agent->model().findVertex({6, 0, 0}), nullptr);
}

TEST_F(SplittingPassTest, DivideEdgeGuardsRejectTooFewSegmentsAndUnknownIdWithNoEvent) {
    kernel.send(AddEdgeRequested{{0, 0, 0}, {8, 0, 0}});
    ASSERT_EQ(changedCount, 1);
    const Id edgeId = agent->model().edges().begin()->first;

    EXPECT_FALSE(agent->divideEdge(edgeId, 1));  // n < 2 -- nothing to divide into
    EXPECT_FALSE(agent->divideEdge(edgeId, 0));
    EXPECT_FALSE(agent->divideEdge(99999, 4));  // unknown edge id

    EXPECT_EQ(agent->model().edges().size(), 1u);
    EXPECT_EQ(changedCount, 1);  // no additional GeometryChanged from any guard
}

// --- followMe deliberately does NOT run the pass ----------------------------

TEST_F(SplittingPassTest, FollowMeDoesNotRunTheSplittingPassEvenAcrossCrossingGeometry) {
    // Triangle profile (0,0,0)-(4,0,0)-(0,3,0), all at z=0.
    kernel.send(AddEdgeRequested{{0, 0, 0}, {4, 0, 0}});
    kernel.send(AddEdgeRequested{{4, 0, 0}, {0, 3, 0}});
    kernel.send(AddEdgeRequested{{0, 3, 0}, {0, 0, 0}});
    ASSERT_EQ(agent->model().faces().size(), 1u);
    const Id faceId = agent->model().faces().begin()->first;
    ASSERT_EQ(changedCount, 3);

    // A pre-existing edge drawn now (nothing to cross yet, so a no-op)
    // positioned so the sweep's own vertical rail (4,0,0)-(4,0,5) will pass
    // straight through its interior at (4,0,2).
    const AddEdgeResult crossing = agent->addEdge({3, 0, 2}, {5, 0, 2});
    ASSERT_TRUE(crossing.created);
    ASSERT_EQ(changedCount, 4);

    const std::vector<Vec3> path = {{0, 0, 0}, {0, 0, 5}};
    const bool changed = agent->followMe(faceId, path, /*closedPath=*/false);

    EXPECT_TRUE(changed);
    EXPECT_EQ(changedCount, 5);

    // followMe never calls runSplittingPass (see its own header comment) --
    // the pre-existing edge stays unsplit and no vertex is created at the
    // geometric crossing, even though the rail edge crosses right through it.
    EXPECT_NE(agent->model().edge(crossing.edge), nullptr);
    EXPECT_EQ(agent->model().findVertex({4, 0, 2}), nullptr);
}

// Regression guard: a 24-segment ring's addPolyline/replaceLastPolyline
// must complete with no spurious splits between near-tangent adjacent
// neighbors -- every vertex stays 2-valent (a real crossing would be 4-valent).
TEST_F(SplittingPassTest, TwentyFourSegmentRingAddPolylineHasNoSpuriousSplitsBetweenNeighbors) {
    const std::vector<Vec3> ring =
        plnr::geo::regularPolygonPoints({0, 0, 0}, 5.0, {0, 0, 1}, 24, {1, 0, 0}, /*circumscribed=*/false);
    ASSERT_EQ(ring.size(), 24u);

    const bool changed = agent->addPolyline(ring, /*closed=*/true);

    EXPECT_TRUE(changed);
    EXPECT_EQ(changedCount, 1);  // one event for the whole ring, no cascade of extras
    EXPECT_EQ(agent->model().edges().size(), 24u);     // no spurious splits -> still exactly 24
    EXPECT_EQ(agent->model().vertices().size(), 24u);  // no spurious split vertices either
    EXPECT_EQ(agent->model().faces().size(), 1u);      // the closed ring fills in as one face

    // Every vertex sees exactly its two polygon neighbors -- 2 outgoing
    // half-edges. A false-positive adjacent-neighbor split (the hypothesis)
    // would instead show extra vertices and/or some vertex with > 2.
    for (const auto& [vertexId, vertex] : agent->model().vertices()) {
        (void)vertexId;
        EXPECT_EQ(vertex.outgoing.size(), 2u);
    }
}

TEST_F(SplittingPassTest, ReplaceLastPolylineOfATwentyFourSegmentRingCompletesWithExactCountsAndNoSpuriousSplits) {
    const std::vector<Vec3> firstRing =
        plnr::geo::regularPolygonPoints({0, 0, 0}, 2.0, {0, 0, 1}, 24, {1, 0, 0}, /*circumscribed=*/false);
    ASSERT_TRUE(agent->addPolyline(firstRing, /*closed=*/true));
    ASSERT_EQ(changedCount, 1);
    ASSERT_EQ(agent->model().edges().size(), 24u);

    // Simulates the VCB retro-edit retype path (CircleTool::onVcbCommit) --
    // a same-shape 24-segment ring at a different radius, like retyping
    // radius/segments after drawing a circle.
    const std::vector<Vec3> secondRing =
        plnr::geo::regularPolygonPoints({0, 0, 0}, 8.0, {0, 0, 1}, 24, {1, 0, 0}, /*circumscribed=*/false);
    const bool replaced = agent->replaceLastPolyline(secondRing, /*closed=*/true);

    EXPECT_TRUE(replaced);
    EXPECT_EQ(changedCount, 2);  // one more event for the whole replace, no cascade of extras
    EXPECT_EQ(agent->model().edges().size(), 24u);     // old ring's 24 edges swapped for a new 24, not accumulated
    EXPECT_EQ(agent->model().vertices().size(), 24u);  // old ring's vertices gone, new ring's are the only ones left
    EXPECT_EQ(agent->model().faces().size(), 1u);

    for (const auto& [vertexId, vertex] : agent->model().vertices()) {
        (void)vertexId;
        EXPECT_EQ(vertex.outgoing.size(), 2u);
        // Every surviving vertex belongs to the NEW (radius 8) ring, not a
        // leftover fragment of the old (radius 2) one.
        EXPECT_NEAR(plnr::geo::length(vertex.pos), 8.0, 1e-6);
    }
}

}  // namespace
