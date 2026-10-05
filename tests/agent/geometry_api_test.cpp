#include "agent/geometry_api.h"

#include <memory>
#include <stdexcept>
#include <vector>

#include <ordo/core/kernel.h>

#include "agent/events.h"
#include "agent/command/geometry_commands.h"

#include <geo/entity.h>
#include <geo/shapes.h>
#include <geo/solid.h>

#include <gtest/gtest.h>

namespace {

using ordo::core::Kernel;
using plnr::agent::AddEdgeCommand;
using plnr::agent::AddPolylineCommand;
using plnr::agent::AddRectangleCommand;
using plnr::agent::DivideEdgeCommand;
using plnr::agent::ExtrudeFaceCommand;
using plnr::agent::GeometryApi;
using plnr::agent::kGeometryApiName;
using plnr::agent::MoveEntityCommand;
using plnr::agent::RemoveEdgeCommand;
using plnr::events::AddEdgeRequested;
using plnr::events::AddPolylineRequested;
using plnr::events::AddRectangleRequested;
using plnr::events::DivideEdgeRequested;
using plnr::events::EntityRef;
using plnr::events::ExtrudeFaceRequested;
using plnr::events::GeometryChanged;
using plnr::events::MoveEntityRequested;
using plnr::events::RemoveEdgeRequested;
using plnr::geo::EntityKind;
using plnr::geo::Id;
using plnr::geo::regularPolygonPoints;
using plnr::geo::Vec3;
using plnr::geo::Vertex;

// Wires a fresh kernel with a registered GeometryApi + AddEdgeCommand, plus
// a GeometryChanged counter subscribed on the kernel's own dispatcher (not
// through a Presenter -- this suite is Qt-free by design).
class GeometryChainTest : public ::testing::Test {
protected:
    void SetUp() override {
        kernel.registerAgent(std::make_shared<GeometryApi>());
        kernel.registerCommand<AddEdgeRequested, AddEdgeCommand>();
        kernel.registerCommand<AddRectangleRequested, AddRectangleCommand>();
        kernel.registerCommand<ExtrudeFaceRequested, ExtrudeFaceCommand>();
        kernel.registerCommand<MoveEntityRequested, MoveEntityCommand>();
        kernel.registerCommand<RemoveEdgeRequested, RemoveEdgeCommand>();
        kernel.registerCommand<AddPolylineRequested, AddPolylineCommand>();
        kernel.registerCommand<DivideEdgeRequested, DivideEdgeCommand>();
        kernel.dispatcher().subscribe<GeometryChanged>(&changedCount,
                                                         [this](const GeometryChanged&) { ++changedCount; });
        agent = kernel.agentAs<GeometryApi>(kGeometryApiName);
    }

    Kernel kernel;
    std::shared_ptr<GeometryApi> agent;
    int changedCount = 0;
};

TEST_F(GeometryChainTest, AddEdgeRequestedAddsEdgeAndFiresGeometryChangedOnce) {
    ASSERT_NE(agent, nullptr);

    kernel.send(AddEdgeRequested{{0, 0, 0}, {1, 0, 0}});

    EXPECT_EQ(agent->model().edges().size(), 1u);
    EXPECT_EQ(changedCount, 1);
}

TEST_F(GeometryChainTest, DuplicateAddEdgeRequestedIsANoOpAndDoesNotRefireGeometryChanged) {
    kernel.send(AddEdgeRequested{{0, 0, 0}, {1, 0, 0}});
    kernel.send(AddEdgeRequested{{0, 0, 0}, {1, 0, 0}});  // same resolved endpoints -- rejected as a duplicate

    EXPECT_EQ(agent->model().edges().size(), 1u);
    EXPECT_EQ(changedCount, 1);  // second request changed nothing -- no second GeometryChanged
}

TEST_F(GeometryChainTest, FourEdgesClosingARectangleCreateExactlyOneFaceAndFireGeometryChangedFourTimes) {
    kernel.send(AddEdgeRequested{{0, 0, 0}, {4, 0, 0}});
    kernel.send(AddEdgeRequested{{4, 0, 0}, {4, 3, 0}});
    kernel.send(AddEdgeRequested{{4, 3, 0}, {0, 3, 0}});
    kernel.send(AddEdgeRequested{{0, 3, 0}, {0, 0, 0}});

    EXPECT_EQ(agent->model().edges().size(), 4u);
    EXPECT_EQ(agent->model().faces().size(), 1u);
    EXPECT_EQ(changedCount, 4);
}

TEST_F(GeometryChainTest, AddRectangleRequestedCreatesFourEdgesFourVerticesAndOneFace) {
    kernel.send(AddRectangleRequested{{0, 0, 0}, {4, 3, 0}});

    EXPECT_EQ(agent->model().edges().size(), 4u);
    EXPECT_EQ(agent->model().vertices().size(), 4u);
    EXPECT_EQ(agent->model().faces().size(), 1u);
    EXPECT_EQ(changedCount, 1);  // addRectangle is coarse-grained -- one GeometryChanged for the whole rectangle
}

TEST_F(GeometryChainTest, AddRectangleRequestedWithSwappedDiagonalCornersProducesSameRectangle) {
    kernel.send(AddRectangleRequested{{4, 0, 0}, {0, 3, 0}});

    EXPECT_EQ(agent->model().edges().size(), 4u);
    EXPECT_EQ(agent->model().vertices().size(), 4u);
    EXPECT_EQ(agent->model().faces().size(), 1u);
    EXPECT_EQ(changedCount, 1);
}

TEST_F(GeometryChainTest, DegenerateAddRectangleRequestedIsANoOp) {
    // dx below kMergeTol -- zero-width rectangle, rejected before any agent call.
    kernel.send(AddRectangleRequested{{0, 0, 0}, {0, 3, 0}});

    EXPECT_EQ(agent->model().edges().size(), 0u);
    EXPECT_EQ(changedCount, 0);
}

TEST_F(GeometryChainTest, AddRectangleDirectOnRegisteredStoreFiresGeometryChangedOnce) {
    ASSERT_NE(agent, nullptr);

    const bool changed = agent->addRectangle({0, 0, 0}, {4, 3, 0});

    EXPECT_TRUE(changed);
    EXPECT_EQ(agent->model().edges().size(), 4u);
    EXPECT_EQ(agent->model().vertices().size(), 4u);
    EXPECT_EQ(agent->model().faces().size(), 1u);
    EXPECT_EQ(changedCount, 1);
}

TEST_F(GeometryChainTest, AddRectangleDirectWithDegenerateSpanReturnsFalseAndDoesNotFireGeometryChanged) {
    // dy below kMergeTol -- zero-height rectangle.
    const bool changed = agent->addRectangle({0, 0, 0}, {4, 0, 0});

    EXPECT_FALSE(changed);
    EXPECT_EQ(agent->model().edges().size(), 0u);
    EXPECT_EQ(changedCount, 0);
}

TEST_F(GeometryChainTest, AddRectangleRequestedIgnoresNonZeroZOnCorners) {
    kernel.send(AddRectangleRequested{{0, 0, 5}, {4, 3, -2}});

    ASSERT_EQ(agent->model().vertices().size(), 4u);
    for (const auto& [id, vertex] : agent->model().vertices()) {
        EXPECT_EQ(vertex.pos.z, 0.0);
    }
}

// Regression pin: Push/Pull on a face of a closed solid MOVES the face
// instead of stacking an additive prism -- see
// the maintainer notes §4 for the "hollow box" bug this guards against.
TEST_F(GeometryChainTest, PushPullInwardOnSolidCapMovesFaceAndKeepsShellClosed) {
    agent->addRectangle({0, 0, 0}, {4, 3, 0});
    const Id baseFace = agent->model().faces().begin()->first;
    const plnr::geo::ExtrudeResult prism = agent->extrudeFace(baseFace, 2.0);
    ASSERT_TRUE(prism.ok);

    const plnr::geo::ExtrudeResult moved = agent->extrudeFace(prism.capFace, -0.5);

    EXPECT_TRUE(moved.ok);
    EXPECT_EQ(moved.capFace, prism.capFace);  // the same face, moved -- nothing dissolved or created
    EXPECT_TRUE(moved.newVertices.empty());
    EXPECT_EQ(agent->model().vertices().size(), 8u);
    EXPECT_EQ(agent->model().edges().size(), 12u);
    EXPECT_EQ(agent->model().faces().size(), 6u);
    EXPECT_TRUE(plnr::geo::isSolid(agent->model()).solid);
    EXPECT_NEAR(plnr::geo::solidVolume(agent->model()), 4.0 * 3.0 * 1.5, 1e-9);
}

TEST_F(GeometryChainTest, PushPullOutwardOnSolidCapGrowsTheBoxWithoutARimJoint) {
    agent->addRectangle({0, 0, 0}, {4, 3, 0});
    const Id baseFace = agent->model().faces().begin()->first;
    const plnr::geo::ExtrudeResult prism = agent->extrudeFace(baseFace, 2.0);
    ASSERT_TRUE(prism.ok);

    const plnr::geo::ExtrudeResult moved = agent->extrudeFace(prism.capFace, 1.0);

    EXPECT_TRUE(moved.ok);
    EXPECT_EQ(agent->model().vertices().size(), 8u);   // no rim ring at the old cap plane
    EXPECT_EQ(agent->model().faces().size(), 6u);      // no interior membrane either
    EXPECT_TRUE(plnr::geo::isSolid(agent->model()).solid);
    EXPECT_NEAR(plnr::geo::solidVolume(agent->model()), 4.0 * 3.0 * 3.0, 1e-9);
}

// Regression pin: an inward face-move PAST the opposite wall must clamp at
// that wall, never drag the loop through it (would globally invert winding)
// -- see the maintainer notes §4.
TEST_F(GeometryChainTest, PushPullInwardPastOppositeWallClampsInsteadOfInverting) {
    agent->addRectangle({0, 0, 0}, {4, 3, 0});
    const Id baseFace = agent->model().faces().begin()->first;
    const plnr::geo::ExtrudeResult prism = agent->extrudeFace(baseFace, 2.0);
    ASSERT_TRUE(prism.ok);

    // Cap sits at z=2; pushing down 5.0 would land at z=-3, well past the
    // z=0 floor. The clamp must stop kMergeTol above the floor instead.
    const plnr::geo::ExtrudeResult moved = agent->extrudeFace(prism.capFace, -5.0);

    EXPECT_TRUE(moved.ok);
    EXPECT_EQ(agent->model().faces().size(), 6u);
    EXPECT_TRUE(plnr::geo::isSolid(agent->model()).solid);  // still closed AND still outward
    const double volume = plnr::geo::solidVolume(agent->model());
    EXPECT_GT(volume, 0.0);                        // the unclamped move inverted this to negative
    EXPECT_LT(volume, 4.0 * 3.0 * 0.01);           // and the box really did shrink to a sliver
}

TEST(GeometryApiNoStoreTest, SendWithNoRegisteredStoreDoesNothing) {
    Kernel kernel;  // GeometryApi deliberately not registered
    kernel.registerCommand<AddEdgeRequested, AddEdgeCommand>();

    EXPECT_NO_THROW(kernel.send(AddEdgeRequested{{0, 0, 0}, {1, 0, 0}}));
    EXPECT_EQ(kernel.agentAs<GeometryApi>(kGeometryApiName), nullptr);
}

// GeometryApi::addEdge() mutates via send(), routed through
// Agent::context() -- unset until registered. Intentional contract
// (the maintainer notes §1): mutations must flow through a registered agent.
TEST(GeometryApiUnregisteredTest, AddEdgeBeforeRegistrationThrowsBecauseContextIsUnset) {
    GeometryApi agent;
    EXPECT_THROW(agent.addEdge({0, 0, 0}, {1, 0, 0}), std::logic_error);
}

TEST_F(GeometryChainTest, ExtrudeFaceRequestedGrowsSixFacesAndFiresExactlyOneMoreGeometryChanged) {
    // 4-edge rectangle -- fires GeometryChanged 4 times and produces 1 face.
    kernel.send(AddEdgeRequested{{0, 0, 0}, {4, 0, 0}});
    kernel.send(AddEdgeRequested{{4, 0, 0}, {4, 3, 0}});
    kernel.send(AddEdgeRequested{{4, 3, 0}, {0, 3, 0}});
    kernel.send(AddEdgeRequested{{0, 3, 0}, {0, 0, 0}});
    ASSERT_EQ(agent->model().faces().size(), 1u);
    ASSERT_EQ(changedCount, 4);

    const Id faceId = agent->model().faces().begin()->first;

    kernel.send(ExtrudeFaceRequested{faceId, 2.0});

    EXPECT_EQ(agent->model().faces().size(), 6u);  // cap + bottom + 4 sides
    EXPECT_EQ(changedCount, 5);                     // exactly one more than before the extrude
}

TEST_F(GeometryChainTest, DegenerateExtrudeFaceRequestedIsANoOp) {
    kernel.send(AddEdgeRequested{{0, 0, 0}, {4, 0, 0}});
    kernel.send(AddEdgeRequested{{4, 0, 0}, {4, 3, 0}});
    kernel.send(AddEdgeRequested{{4, 3, 0}, {0, 3, 0}});
    kernel.send(AddEdgeRequested{{0, 3, 0}, {0, 0, 0}});
    ASSERT_EQ(agent->model().faces().size(), 1u);
    ASSERT_EQ(changedCount, 4);

    const Id faceId = agent->model().faces().begin()->first;

    kernel.send(ExtrudeFaceRequested{faceId, 0.0});  // |distance| < kMergeTol -- rejected

    EXPECT_EQ(agent->model().faces().size(), 1u);
    EXPECT_EQ(agent->model().edges().size(), 4u);
    EXPECT_EQ(changedCount, 4);  // no additional GeometryChanged
}

TEST_F(GeometryChainTest, MoveEntityRequestedMovesVertexAndFiresGeometryChangedOnce) {
    kernel.send(AddEdgeRequested{{0, 0, 0}, {1, 0, 0}});
    ASSERT_EQ(changedCount, 1);

    const Vertex* v0 = agent->model().findVertex({0, 0, 0});
    ASSERT_NE(v0, nullptr);
    const Id vertexId = v0->id;

    kernel.send(MoveEntityRequested{EntityKind::Vertex, vertexId, {1, 0, 0}});

    const Vertex* moved = agent->model().vertex(vertexId);
    ASSERT_NE(moved, nullptr);
    EXPECT_NEAR(moved->pos.x, 1.0, 1e-9);
    EXPECT_NEAR(moved->pos.y, 0.0, 1e-9);
    EXPECT_NEAR(moved->pos.z, 0.0, 1e-9);
    EXPECT_EQ(changedCount, 2);
}

TEST_F(GeometryChainTest, MoveEntityRequestedWithUnknownIdIsANoOp) {
    kernel.send(AddEdgeRequested{{0, 0, 0}, {1, 0, 0}});
    ASSERT_EQ(changedCount, 1);

    kernel.send(MoveEntityRequested{EntityKind::Vertex, 99999, {1, 0, 0}});

    EXPECT_EQ(changedCount, 1);  // no additional GeometryChanged
}

TEST_F(GeometryChainTest, RemoveEdgeRequestedDissolvesFaceAndRemovesEdgeFiringGeometryChangedOnce) {
    kernel.send(AddEdgeRequested{{0, 0, 0}, {4, 0, 0}});
    kernel.send(AddEdgeRequested{{4, 0, 0}, {4, 3, 0}});
    kernel.send(AddEdgeRequested{{4, 3, 0}, {0, 3, 0}});
    kernel.send(AddEdgeRequested{{0, 3, 0}, {0, 0, 0}});
    ASSERT_EQ(agent->model().faces().size(), 1u);
    ASSERT_EQ(agent->model().edges().size(), 4u);
    ASSERT_EQ(changedCount, 4);

    const Id boundaryEdgeId = agent->model().edges().begin()->first;

    kernel.send(RemoveEdgeRequested{boundaryEdgeId});

    EXPECT_EQ(agent->model().faces().size(), 0u);
    EXPECT_EQ(agent->model().edges().size(), 3u);
    EXPECT_EQ(changedCount, 5);
}

TEST_F(GeometryChainTest, RemoveEdgeRequestedWithUnknownIdIsANoOp) {
    kernel.send(AddEdgeRequested{{0, 0, 0}, {1, 0, 0}});
    ASSERT_EQ(changedCount, 1);

    kernel.send(RemoveEdgeRequested{99999});

    EXPECT_EQ(agent->model().edges().size(), 1u);
    EXPECT_EQ(changedCount, 1);  // no additional GeometryChanged
}

TEST_F(GeometryChainTest, AddPolylineRequestedOpenThreePointsCreatesTwoEdgesAndFiresGeometryChangedOnce) {
    kernel.send(AddPolylineRequested{{Vec3{0, 0, 0}, Vec3{1, 0, 0}, Vec3{1, 1, 0}}, false});

    EXPECT_EQ(agent->model().edges().size(), 2u);
    EXPECT_EQ(agent->model().faces().size(), 0u);
    EXPECT_EQ(changedCount, 1);
}

TEST_F(GeometryChainTest, AddPolylineRequestedClosedSquareCreatesFourEdgesOneFaceAndFiresGeometryChangedOnce) {
    kernel.send(AddPolylineRequested{{Vec3{0, 0, 0}, Vec3{4, 0, 0}, Vec3{4, 3, 0}, Vec3{0, 3, 0}}, true});

    EXPECT_EQ(agent->model().edges().size(), 4u);
    EXPECT_EQ(agent->model().faces().size(), 1u);
    EXPECT_EQ(changedCount, 1);
}

TEST_F(GeometryChainTest, AddPolylineDirectWithFewerThanTwoPointsIsANoOpAndDoesNotFireGeometryChanged) {
    EXPECT_FALSE(agent->addPolyline({}, false));
    EXPECT_FALSE(agent->addPolyline({{0, 0, 0}}, false));

    EXPECT_EQ(agent->model().edges().size(), 0u);
    EXPECT_EQ(changedCount, 0);
}

TEST_F(GeometryChainTest, AddPolylineReusingAnExistingVertexMergesTheEndpoint) {
    kernel.send(AddEdgeRequested{{0, 0, 0}, {1, 0, 0}});
    ASSERT_EQ(changedCount, 1);
    ASSERT_EQ(agent->model().vertices().size(), 2u);

    // Starts at (1,0,0) -- the existing edge's other endpoint -- so this
    // point should merge onto it instead of creating a duplicate vertex.
    const bool changed = agent->addPolyline({{1, 0, 0}, {1, 1, 0}}, false);

    EXPECT_TRUE(changed);
    EXPECT_EQ(agent->model().vertices().size(), 3u);  // merged endpoint + one new vertex
    EXPECT_EQ(agent->model().edges().size(), 2u);
    EXPECT_EQ(changedCount, 2);
}

TEST_F(GeometryChainTest, ReplaceLastPolylineSwapsACommittedHexagonForAnOctagon) {
    const std::vector<Vec3> hexagon = regularPolygonPoints({0, 0, 0}, 2.0, Vec3{0, 0, 1}, 6, Vec3{1, 0, 0}, false);
    ASSERT_TRUE(agent->addPolyline(hexagon, true));
    ASSERT_EQ(agent->model().edges().size(), 6u);
    ASSERT_EQ(agent->model().faces().size(), 1u);
    ASSERT_EQ(changedCount, 1);

    std::vector<Id> oldEdgeIds;
    for (const auto& [id, edge] : agent->model().edges()) {
        (void)edge;
        oldEdgeIds.push_back(id);
    }

    const std::vector<Vec3> octagon = regularPolygonPoints({0, 0, 0}, 2.0, Vec3{0, 0, 1}, 8, Vec3{1, 0, 0}, false);
    const bool changed = agent->replaceLastPolyline(octagon, true);

    EXPECT_TRUE(changed);
    EXPECT_EQ(agent->model().edges().size(), 8u);
    EXPECT_EQ(agent->model().faces().size(), 1u);
    EXPECT_EQ(changedCount, 2);  // exactly one more GeometryChanged for the whole swap

    for (Id oldId : oldEdgeIds) {
        EXPECT_EQ(agent->model().edge(oldId), nullptr);  // old hexagon edges are actually gone
    }
}

TEST_F(GeometryChainTest, ReplaceLastPolylineAfterAnUnrelatedAddEdgeIsFalseBecauseTheWindowClosed) {
    const std::vector<Vec3> hexagon = regularPolygonPoints({0, 0, 0}, 2.0, Vec3{0, 0, 1}, 6, Vec3{1, 0, 0}, false);
    ASSERT_TRUE(agent->addPolyline(hexagon, true));
    ASSERT_EQ(changedCount, 1);

    kernel.send(AddEdgeRequested{{10, 10, 0}, {11, 10, 0}});  // unrelated geometry edit -- closes the window
    ASSERT_EQ(changedCount, 2);

    const std::vector<Vec3> octagon = regularPolygonPoints({0, 0, 0}, 2.0, Vec3{0, 0, 1}, 8, Vec3{1, 0, 0}, false);
    const bool changed = agent->replaceLastPolyline(octagon, true);

    EXPECT_FALSE(changed);
    EXPECT_EQ(agent->model().edges().size(), 7u);  // 6 hexagon + 1 unrelated -- octagon never added
    EXPECT_EQ(changedCount, 2);                    // no additional GeometryChanged
}

TEST_F(GeometryChainTest, ReplaceLastPolylineWithNoPriorOpIsFalseAndDoesNotFireGeometryChanged) {
    const bool changed = agent->replaceLastPolyline({{0, 0, 0}, {1, 0, 0}}, false);

    EXPECT_FALSE(changed);
    EXPECT_EQ(agent->model().edges().size(), 0u);
    EXPECT_EQ(changedCount, 0);
}

TEST_F(GeometryChainTest, ReplaceLastPolylinePrunesAHiddenRefOnAReplacedEdge) {
    const std::vector<Vec3> hexagon = regularPolygonPoints({0, 0, 0}, 2.0, Vec3{0, 0, 1}, 6, Vec3{1, 0, 0}, false);
    ASSERT_TRUE(agent->addPolyline(hexagon, true));

    const Id someEdge = agent->model().edges().begin()->first;
    ASSERT_TRUE(agent->setHidden({EntityRef{EntityKind::Edge, someEdge}}, true));
    ASSERT_TRUE(agent->isHidden(EntityRef{EntityKind::Edge, someEdge}));

    const std::vector<Vec3> octagon = regularPolygonPoints({0, 0, 0}, 2.0, Vec3{0, 0, 1}, 8, Vec3{1, 0, 0}, false);
    ASSERT_TRUE(agent->replaceLastPolyline(octagon, true));

    EXPECT_FALSE(agent->isHidden(EntityRef{EntityKind::Edge, someEdge}));
}

TEST_F(GeometryChainTest, DivideEdgeRequestedSplitsIntoNCollinearEdgesFiringGeometryChangedOnce) {
    kernel.send(AddEdgeRequested{{0, 0, 0}, {4, 0, 0}});
    ASSERT_EQ(agent->model().edges().size(), 1u);
    ASSERT_EQ(changedCount, 1);

    const Id edgeId = agent->model().edges().begin()->first;
    kernel.send(DivideEdgeRequested{edgeId, 4});

    EXPECT_EQ(agent->model().edges().size(), 4u);
    EXPECT_EQ(agent->model().vertices().size(), 5u);  // 2 original endpoints + 3 new interior division points
    EXPECT_EQ(changedCount, 2);
}

TEST_F(GeometryChainTest, DivideEdgeRequestedWithNBelowTwoIsANoOp) {
    kernel.send(AddEdgeRequested{{0, 0, 0}, {4, 0, 0}});
    ASSERT_EQ(changedCount, 1);

    kernel.send(DivideEdgeRequested{agent->model().edges().begin()->first, 1});

    EXPECT_EQ(agent->model().edges().size(), 1u);
    EXPECT_EQ(changedCount, 1);  // no additional GeometryChanged
}

TEST_F(GeometryChainTest, DivideEdgeRequestedWithUnknownIdIsANoOp) {
    kernel.send(AddEdgeRequested{{0, 0, 0}, {4, 0, 0}});
    ASSERT_EQ(changedCount, 1);

    kernel.send(DivideEdgeRequested{99999, 4});

    EXPECT_EQ(agent->model().edges().size(), 1u);
    EXPECT_EQ(changedCount, 1);  // no additional GeometryChanged
}

}  // namespace
