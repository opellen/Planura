#include "agent/fog_store.h"

#include <memory>
#include <stdexcept>

#include <ordo/core/app_kernel.h>

#include "agent/events.h"
#include "agent/command/fog_commands.h"

#include <gtest/gtest.h>

// A fresh AppKernel per kernel-based test, direct agent method calls for
// pure agent rules. The "dirty yes, undo no" composition is proven at the
// integration level by tests/agent/undo_test.cpp's FogChangeIsDirtyButNeverCreatesItsOwnUndoStep.
namespace {

using ordo::core::AppKernel;
using plnr::agent::kDefaultFogEndDistance;
using plnr::agent::kDefaultFogStartDistance;
using plnr::agent::kFogStoreName;
using plnr::agent::FogStore;
using plnr::agent::SetFogEnabledCommand;
using plnr::agent::SetFogRangeCommand;
using plnr::agent::SetFogUseBackgroundColorCommand;
using plnr::events::FogChanged;
using plnr::events::SetFogEnabledRequested;
using plnr::events::SetFogRangeRequested;
using plnr::events::SetFogUseBackgroundColorRequested;

// -- Direct agent-method tests (no kernel needed except where set*() must
// dispatch) ------------------------------------------------------------

TEST(FogStoreDefaultsTest, StartsDisabledAtTheDocumentedDefaultRangeAndBackgroundColor) {
    FogStore agent;
    EXPECT_FALSE(agent.enabled());
    EXPECT_EQ(agent.startDistance(), kDefaultFogStartDistance);
    EXPECT_EQ(agent.endDistance(), kDefaultFogEndDistance);
    EXPECT_TRUE(agent.useBackgroundColor());
    EXPECT_EQ(agent.colorR(), 0.5);
    EXPECT_EQ(agent.colorG(), 0.5);
    EXPECT_EQ(agent.colorB(), 0.5);
    EXPECT_TRUE(agent.isAllDefault());
}

// Wires a kernel with a registered FogStore plus all three fog commands,
// and a FogChanged counter subscribed on the kernel's dispatcher for
// assertions.
class FogChainTest : public ::testing::Test {
protected:
    void SetUp() override {
        kernel.registerAgent(std::make_shared<FogStore>());
        kernel.registerCommand<SetFogEnabledRequested, SetFogEnabledCommand>();
        kernel.registerCommand<SetFogRangeRequested, SetFogRangeCommand>();
        kernel.registerCommand<SetFogUseBackgroundColorRequested, SetFogUseBackgroundColorCommand>();
        kernel.dispatcher().subscribe<FogChanged>(&changedCount, [this](const FogChanged&) { ++changedCount; });
        fog = kernel.agentAs<FogStore>(kFogStoreName);
    }

    AppKernel kernel;
    std::shared_ptr<FogStore> fog;
    int changedCount = 0;
};

TEST_F(FogChainTest, SetFogEnabledRequestedChangesEnabledAndFiresOnce) {
    ASSERT_NE(fog, nullptr);
    kernel.send(SetFogEnabledRequested{true});
    EXPECT_TRUE(fog->enabled());
    EXPECT_EQ(changedCount, 1);
    EXPECT_FALSE(fog->isAllDefault());
}

TEST_F(FogChainTest, SetFogEnabledRequestedToSameValueIsANoOp) {
    kernel.send(SetFogEnabledRequested{false});  // already the default
    EXPECT_EQ(changedCount, 0);
}

TEST_F(FogChainTest, SetFogRangeRequestedChangesRangeAndFiresOnce) {
    kernel.send(SetFogRangeRequested{10.0, 80.0});
    EXPECT_EQ(fog->startDistance(), 10.0);
    EXPECT_EQ(fog->endDistance(), 80.0);
    EXPECT_EQ(changedCount, 1);
    EXPECT_FALSE(fog->isAllDefault());
}

TEST_F(FogChainTest, SetFogRangeRequestedToSameValueIsANoOp) {
    kernel.send(SetFogRangeRequested{kDefaultFogStartDistance, kDefaultFogEndDistance});  // already the default
    EXPECT_EQ(changedCount, 0);
}

TEST_F(FogChainTest, PartiallyChangingRangeStillFires) {
    kernel.send(SetFogRangeRequested{kDefaultFogStartDistance, 100.0});  // only endDistance differs
    EXPECT_EQ(changedCount, 1);
    EXPECT_EQ(fog->startDistance(), kDefaultFogStartDistance);
    EXPECT_EQ(fog->endDistance(), 100.0);
}

TEST_F(FogChainTest, SetFogUseBackgroundColorRequestedChangesFlagAndFiresOnce) {
    kernel.send(SetFogUseBackgroundColorRequested{false});
    EXPECT_FALSE(fog->useBackgroundColor());
    EXPECT_EQ(changedCount, 1);
    EXPECT_FALSE(fog->isAllDefault());
}

TEST_F(FogChainTest, SetFogUseBackgroundColorRequestedToSameValueIsANoOp) {
    kernel.send(SetFogUseBackgroundColorRequested{true});  // already the default
    EXPECT_EQ(changedCount, 0);
}

TEST(FogStoreUnregisteredTest, SetEnabledBeforeRegistrationThrowsBecauseContextIsUnset) {
    FogStore agent;
    EXPECT_THROW(agent.setEnabled(true), std::logic_error);
}

TEST(FogStoreUnregisteredTest, SetRangeBeforeRegistrationThrowsBecauseContextIsUnset) {
    FogStore agent;
    EXPECT_THROW(agent.setRange(0.0, 10.0), std::logic_error);
}

TEST(FogStoreUnregisteredTest, SetUseBackgroundColorBeforeRegistrationThrowsBecauseContextIsUnset) {
    FogStore agent;
    EXPECT_THROW(agent.setUseBackgroundColor(false), std::logic_error);
}

// -- isAllDefault() / Restore API ----------------------------------------

TEST(FogStoreRestoreTest, ClearForRestoreResetsEveryFieldToDefault) {
    // set*() dispatches -- needs a registered context, same as every other
    // mutator here (see FogStoreUnregisteredTest above).
    AppKernel kernel;
    kernel.registerAgent(std::make_shared<FogStore>());
    auto fog = kernel.agentAs<FogStore>(kFogStoreName);

    ASSERT_TRUE(fog->setEnabled(true));
    ASSERT_TRUE(fog->setRange(10.0, 80.0));
    ASSERT_TRUE(fog->setUseBackgroundColor(false));
    ASSERT_FALSE(fog->isAllDefault());

    fog->clearForRestore();

    EXPECT_FALSE(fog->enabled());
    EXPECT_EQ(fog->startDistance(), kDefaultFogStartDistance);
    EXPECT_EQ(fog->endDistance(), kDefaultFogEndDistance);
    EXPECT_TRUE(fog->useBackgroundColor());
    EXPECT_TRUE(fog->isAllDefault());
}

// clearForRestore()/restoreXxx() are plain data manipulation -- no
// notification, exercised without ANY kernel registration, proving they
// never reach Agent::send()/context() the way the live setters do.
TEST(FogStoreRestoreTest, RestoreApiDispatchesNothingAndNeedsNoRegistration) {
    FogStore agent;
    agent.restoreEnabled(true);
    agent.restoreRange(10.0, 80.0);
    agent.restoreUseBackgroundColor(false);
    agent.restoreColor(0.1, 0.2, 0.3);

    EXPECT_TRUE(agent.enabled());
    EXPECT_EQ(agent.startDistance(), 10.0);
    EXPECT_EQ(agent.endDistance(), 80.0);
    EXPECT_FALSE(agent.useBackgroundColor());
    EXPECT_EQ(agent.colorR(), 0.1);
    EXPECT_EQ(agent.colorG(), 0.2);
    EXPECT_EQ(agent.colorB(), 0.3);
    EXPECT_FALSE(agent.isAllDefault());

    agent.clearForRestore();  // also dispatches nothing
    EXPECT_TRUE(agent.isAllDefault());
}

// isAllDefault() gates io::writeDocument's "omit the `fog` key entirely"
// path -- each field independently flips it false, and it returns true
// again once every field is restored to default (not just "never touched").
TEST(FogStoreRestoreTest, IsAllDefaultReactsToEachFieldIndependently) {
    FogStore agent;
    EXPECT_TRUE(agent.isAllDefault());

    agent.restoreEnabled(true);
    EXPECT_FALSE(agent.isAllDefault());
    agent.restoreEnabled(false);
    EXPECT_TRUE(agent.isAllDefault());  // back to default -- true again, not "never touched"

    agent.restoreRange(0.0, 10.0);
    EXPECT_FALSE(agent.isAllDefault());
    agent.restoreRange(kDefaultFogStartDistance, kDefaultFogEndDistance);
    EXPECT_TRUE(agent.isAllDefault());

    agent.restoreUseBackgroundColor(false);
    EXPECT_FALSE(agent.isAllDefault());
    agent.restoreUseBackgroundColor(true);
    EXPECT_TRUE(agent.isAllDefault());

    agent.restoreColor(0.1, 0.2, 0.3);
    EXPECT_FALSE(agent.isAllDefault());
    agent.restoreColor(0.5, 0.5, 0.5);
    EXPECT_TRUE(agent.isAllDefault());
}

}  // namespace
