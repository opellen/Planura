#include "agent/geometry_api.h"

#include <memory>
#include <vector>

#include <ordo/core/kernel.h>

#include "agent/events.h"
#include "agent/command/geometry_commands.h"

#include <geo/scene.h>
#include <geo/vec3.h>

#include <gtest/gtest.h>

namespace {

using ordo::core::Kernel;
using plnr::agent::Add3dTextCommand;
using plnr::agent::GeometryApi;
using plnr::agent::kGeometryApiName;
using plnr::events::Add3dTextRequested;
using plnr::events::GeometryChanged;
using plnr::geo::Definition;
using plnr::geo::Id;
using plnr::geo::Instance;
using plnr::geo::Vec3;

// Wires a kernel with a registered GeometryApi + Add3dTextCommand, plus a
// GeometryChanged counter (Qt-free, no Presenter). Exercises the STORE with
// synthetic outlines, NOT Text3dDialog's Qt font polygonization (manual/build verification only).
class Text3dChainTest : public ::testing::Test {
protected:
    void SetUp() override {
        kernel.registerAgent(std::make_shared<GeometryApi>());
        kernel.registerCommand<Add3dTextRequested, Add3dTextCommand>();
        kernel.dispatcher().subscribe<GeometryChanged>(&changedCount,
                                                         [this](const GeometryChanged&) { ++changedCount; });
        agent = kernel.agentAs<GeometryApi>(kGeometryApiName);
    }

    // A unit square outline in the XY plane, z = 0 -- the fixture geometry
    // most tests below feed through add3dText.
    static std::vector<Vec3> square(double x0, double y0) {
        return {
            {x0, y0, 0.0},
            {x0 + 1.0, y0, 0.0},
            {x0 + 1.0, y0 + 1.0, 0.0},
            {x0, y0 + 1.0, 0.0},
        };
    }

    Kernel kernel;
    std::shared_ptr<GeometryApi> agent;
    int changedCount = 0;
};

TEST_F(Text3dChainTest, FlatSquareOutlineCreatesOneInstanceWithFourEdgesAndOneFace) {
    ASSERT_NE(agent, nullptr);

    kernel.send(Add3dTextRequested{"Label", {square(0.0, 0.0)}, 0.0, Vec3{5.0, 0.0, 0.0}});

    ASSERT_EQ(agent->scene().root().children.size(), 1u);
    const Instance& inst = agent->scene().root().children[0];
    const Definition* def = agent->scene().definition(inst.definitionId);
    ASSERT_NE(def, nullptr);
    EXPECT_EQ(def->model.edges().size(), 4u);
    EXPECT_EQ(def->model.faces().size(), 1u);
    EXPECT_FALSE(def->isGroup);  // a COMPONENT, like the reference modeler's 3D Text tool
    EXPECT_EQ(changedCount, 1);
}

TEST_F(Text3dChainTest, ExtrudedSquareOutlineProducesABox) {
    kernel.send(Add3dTextRequested{"Label", {square(0.0, 0.0)}, 2.0, Vec3{}});

    ASSERT_EQ(agent->scene().root().children.size(), 1u);
    const Instance& inst = agent->scene().root().children[0];
    const Definition* def = agent->scene().definition(inst.definitionId);
    ASSERT_NE(def, nullptr);
    EXPECT_EQ(def->model.edges().size(), 12u);  // 4 base + 4 vertical + 4 cap
    EXPECT_EQ(def->model.faces().size(), 6u);   // cap + bottom + 4 sides
    EXPECT_EQ(changedCount, 1);
}

TEST_F(Text3dChainTest, TwoOutlinesBothAppearInTheSameDefinition) {
    kernel.send(Add3dTextRequested{"AB", {square(0.0, 0.0), square(10.0, 0.0)}, 0.0, Vec3{}});

    ASSERT_EQ(agent->scene().root().children.size(), 1u);
    const Instance& inst = agent->scene().root().children[0];
    const Definition* def = agent->scene().definition(inst.definitionId);
    ASSERT_NE(def, nullptr);
    EXPECT_EQ(def->model.edges().size(), 8u);
    EXPECT_EQ(def->model.faces().size(), 2u);
}

TEST_F(Text3dChainTest, EmptyOutlinesListIsANoOp) {
    const int before = changedCount;

    kernel.send(Add3dTextRequested{"Label", {}, 0.0, Vec3{}});

    EXPECT_EQ(agent->scene().root().children.size(), 0u);
    EXPECT_EQ(changedCount, before);
}

TEST_F(Text3dChainTest, EveryOutlineDegenerateIsANoOp) {
    const int before = changedCount;

    // Two points -- not even a triangle.
    kernel.send(Add3dTextRequested{"Label", {{{0, 0, 0}, {1, 0, 0}}}, 0.0, Vec3{}});

    EXPECT_EQ(agent->scene().root().children.size(), 0u);
    EXPECT_EQ(changedCount, before);
}

TEST_F(Text3dChainTest, InstanceTransformTranslationEqualsOrigin) {
    const Vec3 origin{3.0, -6.0, 0.0};

    kernel.send(Add3dTextRequested{"Label", {square(0.0, 0.0)}, 0.0, origin});

    ASSERT_EQ(agent->scene().root().children.size(), 1u);
    const Instance& inst = agent->scene().root().children[0];
    EXPECT_DOUBLE_EQ(inst.transform.t.x, origin.x);
    EXPECT_DOUBLE_EQ(inst.transform.t.y, origin.y);
    EXPECT_DOUBLE_EQ(inst.transform.t.z, origin.z);
}

TEST_F(Text3dChainTest, EmptyNameAutoGeneratesA3dTextName) {
    kernel.send(Add3dTextRequested{"", {square(0.0, 0.0)}, 0.0, Vec3{}});

    ASSERT_EQ(agent->scene().root().children.size(), 1u);
    const Instance& inst = agent->scene().root().children[0];
    const Definition* def = agent->scene().definition(inst.definitionId);
    ASSERT_NE(def, nullptr);
    EXPECT_EQ(inst.name.rfind("3D Text ", 0), 0u);  // starts with "3D Text "
    EXPECT_EQ(def->name.rfind("3D Text ", 0), 0u);
}

TEST(Text3dUnregisteredTest, Add3dTextRequestedWithNoStoreDoesNotCrash) {
    Kernel kernel;  // GeometryApi deliberately not registered
    kernel.registerCommand<Add3dTextRequested, Add3dTextCommand>();

    EXPECT_NO_THROW(kernel.send(Add3dTextRequested{"Label", {{{0, 0, 0}, {1, 0, 0}, {1, 1, 0}}}, 0.0, Vec3{}}));
}

}  // namespace
