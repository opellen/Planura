#include "agent/geometry_api.h"

#include <memory>
#include <vector>

#include <ordo/core/kernel.h>

#include "agent/events.h"
#include "agent/command/geometry_commands.h"

#include <geo/entity.h>

#include <gtest/gtest.h>

namespace {

using ordo::core::Kernel;
using plnr::agent::AddEdgeCommand;
using plnr::agent::FollowMeCommand;
using plnr::agent::GeometryApi;
using plnr::agent::kGeometryApiName;
using plnr::events::AddEdgeRequested;
using plnr::events::FollowMeRequested;
using plnr::events::GeometryChanged;
using plnr::geo::Id;
using plnr::geo::kInvalidId;
using plnr::geo::Vec3;

// Wires a kernel with a registered GeometryApi + AddEdgeCommand +
// FollowMeCommand, plus a GeometryChanged counter on the kernel's
// dispatcher (Qt-free, no Presenter).
class FollowMeTest : public ::testing::Test {
protected:
    void SetUp() override {
        kernel.registerAgent(std::make_shared<GeometryApi>());
        kernel.registerCommand<AddEdgeRequested, AddEdgeCommand>();
        kernel.registerCommand<FollowMeRequested, FollowMeCommand>();
        kernel.dispatcher().subscribe<GeometryChanged>(&changedCount,
                                                         [this](const GeometryChanged&) { ++changedCount; });
        agent = kernel.agentAs<GeometryApi>(kGeometryApiName);
    }

    // Builds a right triangle (0,0,0)-(4,0,0)-(0,3,0) in the XY plane,
    // closing the loop -- 3 edges, 3 vertices, 1 face. Returns the face id.
    Id buildTriangle() {
        kernel.send(AddEdgeRequested{{0, 0, 0}, {4, 0, 0}});
        kernel.send(AddEdgeRequested{{4, 0, 0}, {0, 3, 0}});
        kernel.send(AddEdgeRequested{{0, 3, 0}, {0, 0, 0}});
        return agent->model().faces().begin()->first;
    }

    Kernel kernel;
    std::shared_ptr<GeometryApi> agent;
    int changedCount = 0;
};

TEST_F(FollowMeTest, StraightPathSweepOfATriangleProducesExpectedCounts) {
    const Id faceId = buildTriangle();
    ASSERT_EQ(agent->model().vertices().size(), 3u);
    ASSERT_EQ(agent->model().edges().size(), 3u);
    ASSERT_EQ(agent->model().faces().size(), 1u);
    ASSERT_EQ(changedCount, 3);

    // A straight open path along +Z -- 2 vertices, 1 segment. dir has no
    // joint to rotate around (only one segment), so this is a pure prism
    // sweep, same shape a PushPull extrude would leave behind.
    const std::vector<Vec3> path = {{0, 0, 0}, {0, 0, 5}};

    const bool changed = agent->followMe(faceId, path, /*closedPath=*/false);

    EXPECT_TRUE(changed);
    // 3 original vertices + 3 new (section 1, at z=5) = 6. Section 0 welds
    // exactly back onto the original triangle's own 3 vertices.
    EXPECT_EQ(agent->model().vertices().size(), 6u);
    // 3 original (triangle) + 3 (section 1's own loop) + 3 (rail edges
    // section0 -> section1) = 9.
    EXPECT_EQ(agent->model().edges().size(), 9u);
    // 1 original (start cap, left in place) + 3 side quads (1 strip *
    // 3 profile vertices) + 1 end cap (section 1's own loop, open path) = 5.
    EXPECT_EQ(agent->model().faces().size(), 5u);
    EXPECT_EQ(changedCount, 4);  // exactly one more GeometryChanged for the whole sweep

    // The swept end-cap loop landed exactly at z=5, translated (no rotation
    // -- a single-segment path has no joint).
    EXPECT_NE(agent->model().findVertex({0, 0, 5}), nullptr);
    EXPECT_NE(agent->model().findVertex({4, 0, 5}), nullptr);
    EXPECT_NE(agent->model().findVertex({0, 3, 5}), nullptr);
    // The original face's own geometry is untouched.
    EXPECT_NE(agent->model().findVertex({0, 0, 0}), nullptr);
    EXPECT_NE(agent->model().findVertex({4, 0, 0}), nullptr);
    EXPECT_NE(agent->model().findVertex({0, 3, 0}), nullptr);
}

TEST_F(FollowMeTest, UnknownFaceIdIsANoOpAndDoesNotFireGeometryChanged) {
    const std::vector<Vec3> path = {{0, 0, 0}, {0, 0, 5}};

    const bool changed = agent->followMe(kInvalidId, path, /*closedPath=*/false);

    EXPECT_FALSE(changed);
    EXPECT_EQ(agent->model().vertices().size(), 0u);
    EXPECT_EQ(changedCount, 0);
}

TEST_F(FollowMeTest, DegenerateSweepIsANoOpAndDoesNotFireGeometryChanged) {
    const Id faceId = buildTriangle();
    ASSERT_EQ(changedCount, 3);

    // path.size() < 2 -- geo::sweep's own guard rejects this outright.
    const std::vector<Vec3> path = {{0, 0, 0}};

    const bool changed = agent->followMe(faceId, path, /*closedPath=*/false);

    EXPECT_FALSE(changed);
    EXPECT_EQ(agent->model().vertices().size(), 3u);  // unchanged
    EXPECT_EQ(changedCount, 3);                        // no additional GeometryChanged
}

TEST_F(FollowMeTest, GeometryChangedFiresExactlyOnceViaTheCommandRoundTrip) {
    const Id faceId = buildTriangle();
    ASSERT_EQ(changedCount, 3);

    const std::vector<Vec3> path = {{0, 0, 0}, {0, 0, 5}};
    kernel.send(FollowMeRequested{faceId, path, /*closedPath=*/false});

    EXPECT_EQ(changedCount, 4);  // exactly one more, through the full event -> Command -> Agent round trip
    EXPECT_EQ(agent->model().faces().size(), 5u);
}

}  // namespace
