#include "agent/guide_store.h"

#include <cmath>
#include <memory>
#include <stdexcept>
#include <vector>

#include <ordo/core/app_kernel.h>

#include "agent/events.h"
#include "agent/command/guide_commands.h"

#include <geo/infer.h>
#include <geo/vec3.h>

#include <gtest/gtest.h>

namespace {

using ordo::core::AppKernel;
using plnr::agent::AddGuideLineCommand;
using plnr::agent::AddGuidePointCommand;
using plnr::agent::DeleteAllGuidesCommand;
using plnr::agent::EraseGuideCommand;
using plnr::agent::GuideStore;
using plnr::agent::kGuideStoreName;
using plnr::agent::SetGuideHiddenCommand;
using plnr::events::AddGuideLineRequested;
using plnr::events::AddGuidePointRequested;
using plnr::events::DeleteAllGuidesRequested;
using plnr::events::EraseGuideRequested;
using plnr::events::GuidesChanged;
using plnr::events::SetGuideHiddenRequested;
using plnr::geo::Vec3;

// Wires a kernel with a registered GuideStore plus all 5 guide commands,
// and a GuidesChanged counter on the kernel's dispatcher (Qt-free, no Presenter).
class GuideChainTest : public ::testing::Test {
protected:
    void SetUp() override {
        kernel.registerAgent(std::make_shared<GuideStore>());
        kernel.registerCommand<AddGuideLineRequested, AddGuideLineCommand>();
        kernel.registerCommand<AddGuidePointRequested, AddGuidePointCommand>();
        kernel.registerCommand<EraseGuideRequested, EraseGuideCommand>();
        kernel.registerCommand<DeleteAllGuidesRequested, DeleteAllGuidesCommand>();
        kernel.registerCommand<SetGuideHiddenRequested, SetGuideHiddenCommand>();
        kernel.dispatcher().subscribe<GuidesChanged>(&changedCount, [this](const GuidesChanged&) { ++changedCount; });
        guides = kernel.agentAs<GuideStore>(kGuideStoreName);
    }

    AppKernel kernel;
    std::shared_ptr<GuideStore> guides;
    int changedCount = 0;
};

TEST_F(GuideChainTest, StoreStartsEmpty) {
    ASSERT_NE(guides, nullptr);
    EXPECT_TRUE(guides->guides().empty());
    EXPECT_TRUE(guides->lineView().empty());
    EXPECT_TRUE(guides->pointView().empty());
}

TEST_F(GuideChainTest, AddGuideLineRequestedNormalizesDirAndFiresOnce) {
    kernel.send(AddGuideLineRequested{Vec3{1.0, 2.0, 3.0}, Vec3{2.0, 0.0, 0.0}});

    ASSERT_EQ(guides->guides().size(), 1u);
    const plnr::agent::Guide& g = guides->guides().front();
    EXPECT_TRUE(g.isLine);
    EXPECT_EQ(g.point.x, 1.0);
    EXPECT_NEAR(g.dir.x, 1.0, 1e-9);  // normalized from {2,0,0}
    EXPECT_NEAR(g.dir.y, 0.0, 1e-9);
    EXPECT_NEAR(g.dir.z, 0.0, 1e-9);
    EXPECT_EQ(changedCount, 1);
}

TEST_F(GuideChainTest, AddGuideLineReturnsIncreasingIds) {
    const plnr::geo::Id first = guides->addGuideLine({0, 0, 0}, {1, 0, 0});
    const plnr::geo::Id second = guides->addGuideLine({0, 0, 0}, {0, 1, 0});

    EXPECT_NE(first, plnr::geo::kInvalidId);
    EXPECT_NE(second, plnr::geo::kInvalidId);
    EXPECT_NE(first, second);
}

TEST_F(GuideChainTest, AddGuidePointRequestedFiresOnce) {
    kernel.send(AddGuidePointRequested{Vec3{4.0, 5.0, 6.0}});

    ASSERT_EQ(guides->guides().size(), 1u);
    const plnr::agent::Guide& g = guides->guides().front();
    EXPECT_FALSE(g.isLine);
    EXPECT_EQ(g.point.x, 4.0);
    EXPECT_EQ(g.point.y, 5.0);
    EXPECT_EQ(g.point.z, 6.0);
    EXPECT_EQ(changedCount, 1);
}

TEST_F(GuideChainTest, EraseGuideRequestedRemovesItAndFiresOnce) {
    const plnr::geo::Id id = guides->addGuideLine({0, 0, 0}, {1, 0, 0});
    ASSERT_EQ(guides->guides().size(), 1u);
    changedCount = 0;

    kernel.send(EraseGuideRequested{id});

    EXPECT_TRUE(guides->guides().empty());
    EXPECT_EQ(changedCount, 1);
}

TEST_F(GuideChainTest, EraseGuideRequestedWithUnknownIdIsANoOp) {
    guides->addGuideLine({0, 0, 0}, {1, 0, 0});
    changedCount = 0;

    kernel.send(EraseGuideRequested{424242});

    EXPECT_EQ(guides->guides().size(), 1u);
    EXPECT_EQ(changedCount, 0);
}

TEST_F(GuideChainTest, SetGuideHiddenRequestedTrueHidesAndFiresOnce) {
    const plnr::geo::Id id = guides->addGuideLine({0, 0, 0}, {1, 0, 0});
    changedCount = 0;

    kernel.send(SetGuideHiddenRequested{id, true});

    EXPECT_TRUE(guides->hidden(id));
    EXPECT_EQ(changedCount, 1);
}

TEST_F(GuideChainTest, SetGuideHiddenRequestedToSameValueIsANoOp) {
    const plnr::geo::Id id = guides->addGuideLine({0, 0, 0}, {1, 0, 0});
    changedCount = 0;

    kernel.send(SetGuideHiddenRequested{id, false});  // already visible

    EXPECT_FALSE(guides->hidden(id));
    EXPECT_EQ(changedCount, 0);
}

TEST_F(GuideChainTest, SetGuideHiddenRequestedWithUnknownIdIsANoOp) {
    kernel.send(SetGuideHiddenRequested{424242, true});

    EXPECT_EQ(changedCount, 0);
}

TEST_F(GuideChainTest, HiddenOnUnknownIdReadsAsFalse) {
    EXPECT_FALSE(guides->hidden(424242));
}

TEST_F(GuideChainTest, DeleteAllGuidesRequestedClearsEverythingAndFiresOnce) {
    guides->addGuideLine({0, 0, 0}, {1, 0, 0});
    guides->addGuidePoint({1, 1, 1});
    ASSERT_EQ(guides->guides().size(), 2u);
    changedCount = 0;

    kernel.send(DeleteAllGuidesRequested{});

    EXPECT_TRUE(guides->guides().empty());
    EXPECT_EQ(changedCount, 1);
}

TEST_F(GuideChainTest, DeleteAllGuidesRequestedWhenEmptyIsANoOp) {
    ASSERT_TRUE(guides->guides().empty());

    kernel.send(DeleteAllGuidesRequested{});

    EXPECT_EQ(changedCount, 0);
}

TEST_F(GuideChainTest, LineViewOnlyIncludesLinesAndExcludesHidden) {
    const plnr::geo::Id visibleLine = guides->addGuideLine({0, 0, 0}, {1, 0, 0});
    const plnr::geo::Id hiddenLine = guides->addGuideLine({1, 1, 1}, {0, 1, 0});
    guides->addGuidePoint({2, 2, 2});  // must not show up in lineView()
    guides->setHidden(hiddenLine, true);

    const std::vector<plnr::geo::GuideLineData> lines = guides->lineView();

    ASSERT_EQ(lines.size(), 1u);
    EXPECT_EQ(lines.front().id, visibleLine);
}

TEST_F(GuideChainTest, PointViewOnlyIncludesPointsAndExcludesHidden) {
    guides->addGuideLine({0, 0, 0}, {1, 0, 0});  // must not show up in pointView()
    const plnr::geo::Id visiblePoint = guides->addGuidePoint({1, 1, 1});
    const plnr::geo::Id hiddenPoint = guides->addGuidePoint({2, 2, 2});
    guides->setHidden(hiddenPoint, true);

    const std::vector<plnr::geo::GuidePointData> points = guides->pointView();

    ASSERT_EQ(points.size(), 1u);
    EXPECT_EQ(points.front().id, visiblePoint);
}

TEST_F(GuideChainTest, UnhidingRestoresViewMembership) {
    const plnr::geo::Id id = guides->addGuideLine({0, 0, 0}, {1, 0, 0});
    guides->setHidden(id, true);
    ASSERT_TRUE(guides->lineView().empty());

    guides->setHidden(id, false);

    ASSERT_EQ(guides->lineView().size(), 1u);
    EXPECT_EQ(guides->lineView().front().id, id);
}

TEST(GuideStoreUnregisteredTest, AddGuideLineBeforeRegistrationThrowsBecauseContextIsUnset) {
    GuideStore agent;
    EXPECT_THROW(agent.addGuideLine({0, 0, 0}, {1, 0, 0}), std::logic_error);
}

TEST(GuideStoreUnregisteredTest, EraseOnEmptyUnregisteredStoreDoesNotThrow) {
    // erase() on an unknown id sends nothing, so it never touches context()
    // -- no agent registration required, mirroring
    // SelectionStoreUnregisteredTest::ClearOnEmptyUnregisteredStoreDoesNotThrow.
    GuideStore agent;
    EXPECT_NO_THROW(EXPECT_FALSE(agent.erase(1)));
}

}  // namespace
