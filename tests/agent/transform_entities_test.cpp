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
using plnr::agent::GeometryApi;
using plnr::agent::kGeometryApiName;
using plnr::agent::TransformEntitiesCommand;
using plnr::events::AddEdgeRequested;
using plnr::events::EntityRef;
using plnr::events::GeometryChanged;
using plnr::events::TransformSpec;
using plnr::geo::EntityKind;
using plnr::geo::Id;
using plnr::geo::Vec3;

// Not <cmath>'s M_PI (MSVC hides it behind _USE_MATH_DEFINES) -- a local
// constant is simplest for the one rotation test below.
constexpr double kHalfPi = 1.5707963267948966;

// Wires a kernel with a registered GeometryApi + AddEdgeCommand +
// TransformEntitiesCommand, plus a GeometryChanged counter on the
// kernel's dispatcher (Qt-free, no Presenter).
class TransformEntitiesTest : public ::testing::Test {
protected:
    void SetUp() override {
        kernel.registerAgent(std::make_shared<GeometryApi>());
        kernel.registerCommand<AddEdgeRequested, AddEdgeCommand>();
        kernel.registerCommand<plnr::events::TransformEntitiesRequested, TransformEntitiesCommand>();
        kernel.dispatcher().subscribe<GeometryChanged>(&changedCount,
                                                         [this](const GeometryChanged&) { ++changedCount; });
        agent = kernel.agentAs<GeometryApi>(kGeometryApiName);
    }

    // Builds a 4x3 rectangle (0,0,0)-(4,0,0)-(4,3,0)-(0,3,0), closing the
    // loop -- 4 edges, 4 vertices, 1 face, same shape geometry_api_test.cpp
    // builds by hand. Returns the face id.
    Id buildRectangle() {
        kernel.send(AddEdgeRequested{{0, 0, 0}, {4, 0, 0}});
        kernel.send(AddEdgeRequested{{4, 0, 0}, {4, 3, 0}});
        kernel.send(AddEdgeRequested{{4, 3, 0}, {0, 3, 0}});
        kernel.send(AddEdgeRequested{{0, 3, 0}, {0, 0, 0}});
        return agent->model().faces().begin()->first;
    }

    Kernel kernel;
    std::shared_ptr<GeometryApi> agent;
    int changedCount = 0;
};

TEST_F(TransformEntitiesTest, InPlaceRotateOfAFaceClosureMovesItsVertices) {
    const Id faceId = buildRectangle();
    ASSERT_EQ(agent->model().vertices().size(), 4u);
    ASSERT_EQ(changedCount, 4);

    TransformSpec spec;
    spec.kind = TransformSpec::Kind::Rotation;
    spec.point = {0, 0, 0};
    spec.axis = {0, 0, 1};
    spec.angleRad = kHalfPi;  // 90 deg CCW about Z: (x, y) -> (-y, x)

    const bool changed = agent->transformEntities({EntityRef{EntityKind::Face, faceId}}, spec, /*copies=*/0);

    EXPECT_TRUE(changed);
    EXPECT_EQ(agent->model().vertices().size(), 4u);  // moved in place, none added/removed
    EXPECT_EQ(agent->model().edges().size(), 4u);
    EXPECT_EQ(agent->model().faces().size(), 1u);
    EXPECT_EQ(changedCount, 5);  // exactly one more GeometryChanged for the whole rotate

    EXPECT_NE(agent->model().findVertex({0, 4, 0}), nullptr);   // (4,0,0) rotated here
    EXPECT_NE(agent->model().findVertex({-3, 4, 0}), nullptr);  // (4,3,0) rotated here
    EXPECT_NE(agent->model().findVertex({-3, 0, 0}), nullptr);  // (0,3,0) rotated here
    EXPECT_EQ(agent->model().findVertex({4, 0, 0}), nullptr);   // old position vacated
}

TEST_F(TransformEntitiesTest, CopiesEqualsTwoTranslationLeavesOriginalsAndCreatesTwoCopies) {
    const Id faceId = buildRectangle();
    ASSERT_EQ(changedCount, 4);

    TransformSpec spec;
    spec.kind = TransformSpec::Kind::Translation;
    spec.delta = {10, 0, 0};

    const bool changed = agent->transformEntities({EntityRef{EntityKind::Face, faceId}}, spec, /*copies=*/2);

    EXPECT_TRUE(changed);
    EXPECT_EQ(agent->model().edges().size(), 12u);     // 4 original + 2 * 4 copy
    EXPECT_EQ(agent->model().vertices().size(), 12u);  // 4 original + 2 * 4 copy
    EXPECT_EQ(agent->model().faces().size(), 3u);      // 1 original + 2 copy
    EXPECT_EQ(changedCount, 5);                        // exactly one more GeometryChanged for the whole call

    // Originals untouched.
    EXPECT_NE(agent->model().findVertex({0, 0, 0}), nullptr);
    EXPECT_NE(agent->model().findVertex({4, 0, 0}), nullptr);
    // Copy 1 at 1x delta, copy 2 at 2x delta.
    EXPECT_NE(agent->model().findVertex({10, 0, 0}), nullptr);
    EXPECT_NE(agent->model().findVertex({14, 0, 0}), nullptr);
    EXPECT_NE(agent->model().findVertex({20, 0, 0}), nullptr);
    EXPECT_NE(agent->model().findVertex({24, 0, 0}), nullptr);
}

TEST_F(TransformEntitiesTest, EmptyRefsIsANoOpAndDoesNotFireGeometryChanged) {
    TransformSpec spec;
    spec.kind = TransformSpec::Kind::Translation;
    spec.delta = {1, 0, 0};

    const bool changed = agent->transformEntities({}, spec, /*copies=*/0);

    EXPECT_FALSE(changed);
    EXPECT_EQ(agent->model().vertices().size(), 0u);
    EXPECT_EQ(changedCount, 0);
}

TEST_F(TransformEntitiesTest, ApplyArrayTimesAfterCopiesEqualsOneYieldsThreeCopiesTotal) {
    const Id faceId = buildRectangle();
    ASSERT_EQ(changedCount, 4);

    TransformSpec spec;
    spec.kind = TransformSpec::Kind::Translation;
    spec.delta = {10, 0, 0};

    ASSERT_TRUE(agent->transformEntities({EntityRef{EntityKind::Face, faceId}}, spec, /*copies=*/1));
    ASSERT_EQ(agent->model().edges().size(), 8u);
    ASSERT_EQ(changedCount, 5);

    const bool retyped = agent->applyArrayTimes(3);

    EXPECT_TRUE(retyped);
    EXPECT_EQ(agent->model().edges().size(), 16u);     // 4 original + 3 * 4 copy
    EXPECT_EQ(agent->model().vertices().size(), 16u);  // 4 original + 3 * 4 copy
    EXPECT_EQ(agent->model().faces().size(), 4u);      // 1 original + 3 copy
    EXPECT_EQ(changedCount, 6);                        // exactly one more GeometryChanged for the retype

    EXPECT_NE(agent->model().findVertex({10, 0, 0}), nullptr);  // 1x
    EXPECT_NE(agent->model().findVertex({20, 0, 0}), nullptr);  // 2x
    EXPECT_NE(agent->model().findVertex({30, 0, 0}), nullptr);  // 3x
}

TEST_F(TransformEntitiesTest, ApplyArrayDivideYieldsCopiesAtHalfAndFullStep) {
    const Id faceId = buildRectangle();
    ASSERT_EQ(changedCount, 4);

    TransformSpec spec;
    spec.kind = TransformSpec::Kind::Translation;
    spec.delta = {10, 0, 0};

    ASSERT_TRUE(agent->transformEntities({EntityRef{EntityKind::Face, faceId}}, spec, /*copies=*/1));
    ASSERT_EQ(changedCount, 5);

    const bool retyped = agent->applyArrayDivide(2);

    EXPECT_TRUE(retyped);
    EXPECT_EQ(agent->model().edges().size(), 12u);     // 4 original + 2 * 4 copy
    EXPECT_EQ(agent->model().vertices().size(), 12u);  // 4 original + 2 * 4 copy
    EXPECT_EQ(agent->model().faces().size(), 3u);      // 1 original + 2 copy
    EXPECT_EQ(changedCount, 6);                        // exactly one more GeometryChanged for the retype

    EXPECT_NE(agent->model().findVertex({5, 0, 0}), nullptr);   // half step (1/2)
    EXPECT_NE(agent->model().findVertex({9, 0, 0}), nullptr);   // half step, other corner
    EXPECT_NE(agent->model().findVertex({10, 0, 0}), nullptr);  // full step (2/2)
    EXPECT_NE(agent->model().findVertex({14, 0, 0}), nullptr);  // full step, other corner
}

TEST_F(TransformEntitiesTest, AnotherMutatorClosesTheWindowSoApplyArrayTimesReturnsFalse) {
    const Id faceId = buildRectangle();
    ASSERT_EQ(changedCount, 4);

    TransformSpec spec;
    spec.kind = TransformSpec::Kind::Translation;
    spec.delta = {10, 0, 0};

    ASSERT_TRUE(agent->transformEntities({EntityRef{EntityKind::Face, faceId}}, spec, /*copies=*/1));
    ASSERT_EQ(agent->model().edges().size(), 8u);
    ASSERT_EQ(changedCount, 5);

    kernel.send(AddEdgeRequested{{100, 100, 0}, {101, 100, 0}});  // unrelated edit -- closes the array window
    ASSERT_EQ(agent->model().edges().size(), 9u);
    ASSERT_EQ(changedCount, 6);

    const bool retyped = agent->applyArrayTimes(3);

    EXPECT_FALSE(retyped);
    EXPECT_EQ(agent->model().edges().size(), 9u);  // unchanged -- window was closed
    EXPECT_EQ(changedCount, 6);                    // no additional GeometryChanged
}

}  // namespace
