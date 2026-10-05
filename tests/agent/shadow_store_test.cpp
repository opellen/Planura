#include "agent/shadow_store.h"

#include <memory>
#include <stdexcept>

#include <ordo/core/kernel.h>

#include "agent/events.h"
#include "agent/command/shadow_commands.h"

#include <gtest/gtest.h>

namespace {

using ordo::core::Kernel;
using plnr::agent::kDefaultDark;
using plnr::agent::kDefaultDay;
using plnr::agent::kDefaultHourLocal;
using plnr::agent::kDefaultLatitudeDeg;
using plnr::agent::kDefaultLight;
using plnr::agent::kDefaultLongitudeDeg;
using plnr::agent::kDefaultMonth;
using plnr::agent::kShadowStoreName;
using plnr::agent::SetShadowDarkCommand;
using plnr::agent::SetShadowLightCommand;
using plnr::agent::SetShowShadowsCommand;
using plnr::agent::SetSunDateTimeCommand;
using plnr::agent::SetSunPositionCommand;
using plnr::agent::SetUseSunForShadingCommand;
using plnr::agent::ShadowStore;
using plnr::events::SetShadowDarkRequested;
using plnr::events::SetShadowLightRequested;
using plnr::events::SetShowShadowsRequested;
using plnr::events::SetSunDateTimeRequested;
using plnr::events::SetSunPositionRequested;
using plnr::events::SetUseSunForShadingRequested;
using plnr::events::ShadowsChanged;

// -- Direct agent-method tests (no kernel needed except where set*() must
// dispatch) ------------------------------------------------------------

TEST(ShadowStoreDefaultsTest, StartsWithDefaultFlagsAtTheDocumentedDefaultSunPositionAndSliders) {
    ShadowStore agent;
    // useSunForShading defaults ON (matches the reference modeler: sun shading
    // active with shadows off); showShadows stays off.
    EXPECT_EQ(agent.useSunForShading(), plnr::agent::kDefaultUseSunForShading);
    EXPECT_FALSE(agent.showShadows());
    EXPECT_EQ(agent.latitudeDeg(), kDefaultLatitudeDeg);
    EXPECT_EQ(agent.longitudeDeg(), kDefaultLongitudeDeg);
    EXPECT_EQ(agent.month(), kDefaultMonth);
    EXPECT_EQ(agent.day(), kDefaultDay);
    EXPECT_EQ(agent.hourLocal(), kDefaultHourLocal);
    EXPECT_EQ(agent.light(), kDefaultLight);
    EXPECT_EQ(agent.dark(), kDefaultDark);
    EXPECT_TRUE(agent.isAllDefault());
}

// Wires a fresh kernel with ShadowStore + all six shadow commands, plus a
// ShadowsChanged counter on the dispatcher.
class ShadowChainTest : public ::testing::Test {
protected:
    void SetUp() override {
        kernel.registerAgent(std::make_shared<ShadowStore>());
        kernel.registerCommand<SetUseSunForShadingRequested, SetUseSunForShadingCommand>();
        kernel.registerCommand<SetShowShadowsRequested, SetShowShadowsCommand>();
        kernel.registerCommand<SetSunPositionRequested, SetSunPositionCommand>();
        kernel.registerCommand<SetSunDateTimeRequested, SetSunDateTimeCommand>();
        kernel.registerCommand<SetShadowLightRequested, SetShadowLightCommand>();
        kernel.registerCommand<SetShadowDarkRequested, SetShadowDarkCommand>();
        kernel.dispatcher().subscribe<ShadowsChanged>(&changedCount, [this](const ShadowsChanged&) { ++changedCount; });
        shadow = kernel.agentAs<ShadowStore>(kShadowStoreName);
    }

    Kernel kernel;
    std::shared_ptr<ShadowStore> shadow;
    int changedCount = 0;
};

TEST_F(ShadowChainTest, SetUseSunForShadingRequestedChangesFlagAndFiresOnce) {
    ASSERT_NE(shadow, nullptr);
    // useSunForShading defaults ON -- OFF is the change that must fire and
    // flip isAllDefault() false.
    kernel.send(SetUseSunForShadingRequested{false});
    EXPECT_FALSE(shadow->useSunForShading());
    EXPECT_FALSE(shadow->showShadows());  // independent of the other flag
    EXPECT_EQ(changedCount, 1);
    EXPECT_FALSE(shadow->isAllDefault());
}

TEST_F(ShadowChainTest, SetUseSunForShadingRequestedToSameValueIsANoOp) {
    kernel.send(SetUseSunForShadingRequested{true});  // already the default
    EXPECT_EQ(changedCount, 0);
}

TEST_F(ShadowChainTest, SetShowShadowsRequestedChangesFlagAndFiresOnce) {
    ASSERT_NE(shadow, nullptr);
    kernel.send(SetShowShadowsRequested{true});
    EXPECT_TRUE(shadow->showShadows());
    EXPECT_TRUE(shadow->useSunForShading());  // independent of the other flag -- stays at its ON default
    EXPECT_EQ(changedCount, 1);
    EXPECT_FALSE(shadow->isAllDefault());
}

TEST_F(ShadowChainTest, SetShowShadowsRequestedToSameValueIsANoOp) {
    kernel.send(SetShowShadowsRequested{false});  // already the default
    EXPECT_EQ(changedCount, 0);
}

TEST_F(ShadowChainTest, BothFlagsToggleIndependently) {
    // Each flag flipped away from its own default (useSunForShading ON,
    // showShadows OFF) -- two real changes, two events, neither disturbs
    // the other's value.
    kernel.send(SetUseSunForShadingRequested{false});
    kernel.send(SetShowShadowsRequested{true});
    EXPECT_FALSE(shadow->useSunForShading());
    EXPECT_TRUE(shadow->showShadows());
    EXPECT_EQ(changedCount, 2);
}

TEST_F(ShadowChainTest, SetSunPositionRequestedChangesPositionAndFiresOnce) {
    kernel.send(SetSunPositionRequested{51.48, -0.08});  // Greenwich-ish, an arbitrary non-default pick
    EXPECT_EQ(shadow->latitudeDeg(), 51.48);
    EXPECT_EQ(shadow->longitudeDeg(), -0.08);
    EXPECT_EQ(changedCount, 1);
    EXPECT_FALSE(shadow->isAllDefault());
}

TEST_F(ShadowChainTest, SetSunPositionRequestedToSameValueIsANoOp) {
    kernel.send(SetSunPositionRequested{kDefaultLatitudeDeg, kDefaultLongitudeDeg});  // already the default
    EXPECT_EQ(changedCount, 0);
}

TEST_F(ShadowChainTest, SetSunDateTimeRequestedChangesDateTimeAndFiresOnce) {
    kernel.send(SetSunDateTimeRequested{6, 21, 9.0});
    EXPECT_EQ(shadow->month(), 6);
    EXPECT_EQ(shadow->day(), 21);
    EXPECT_EQ(shadow->hourLocal(), 9.0);
    EXPECT_EQ(changedCount, 1);
    EXPECT_FALSE(shadow->isAllDefault());
}

TEST_F(ShadowChainTest, SetSunDateTimeRequestedToSameValueIsANoOp) {
    kernel.send(SetSunDateTimeRequested{kDefaultMonth, kDefaultDay, kDefaultHourLocal});  // already the default
    EXPECT_EQ(changedCount, 0);
}

TEST_F(ShadowChainTest, PartiallyChangingDateTimeStillFires) {
    kernel.send(SetSunDateTimeRequested{kDefaultMonth, kDefaultDay, 10.0});  // only hourLocal differs
    EXPECT_EQ(changedCount, 1);
    EXPECT_EQ(shadow->month(), kDefaultMonth);
    EXPECT_EQ(shadow->day(), kDefaultDay);
    EXPECT_EQ(shadow->hourLocal(), 10.0);
}

TEST_F(ShadowChainTest, SetShadowLightRequestedChangesLightAndFiresOnce) {
    kernel.send(SetShadowLightRequested{50.0});
    EXPECT_EQ(shadow->light(), 50.0);
    EXPECT_EQ(changedCount, 1);
    EXPECT_FALSE(shadow->isAllDefault());
}

TEST_F(ShadowChainTest, SetShadowLightRequestedToSameValueIsANoOp) {
    kernel.send(SetShadowLightRequested{kDefaultLight});  // already the default
    EXPECT_EQ(changedCount, 0);
}

TEST_F(ShadowChainTest, SetShadowDarkRequestedChangesDarkAndFiresOnce) {
    kernel.send(SetShadowDarkRequested{20.0});
    EXPECT_EQ(shadow->dark(), 20.0);
    EXPECT_EQ(changedCount, 1);
    EXPECT_FALSE(shadow->isAllDefault());
}

TEST_F(ShadowChainTest, SetShadowDarkRequestedToSameValueIsANoOp) {
    kernel.send(SetShadowDarkRequested{kDefaultDark});  // already the default
    EXPECT_EQ(changedCount, 0);
}

TEST(ShadowStoreUnregisteredTest, SetUseSunForShadingBeforeRegistrationThrowsBecauseContextIsUnset) {
    ShadowStore agent;
    // false is the non-default value here -- true would no-op before
    // reaching the throwing send(), and EXPECT_THROW would fail.
    EXPECT_THROW(agent.setUseSunForShading(false), std::logic_error);
}

TEST(ShadowStoreUnregisteredTest, SetShowShadowsBeforeRegistrationThrowsBecauseContextIsUnset) {
    ShadowStore agent;
    EXPECT_THROW(agent.setShowShadows(true), std::logic_error);
}

TEST(ShadowStoreUnregisteredTest, SetPositionBeforeRegistrationThrowsBecauseContextIsUnset) {
    ShadowStore agent;
    EXPECT_THROW(agent.setPosition(0.0, 0.0), std::logic_error);
}

TEST(ShadowStoreUnregisteredTest, SetDateTimeBeforeRegistrationThrowsBecauseContextIsUnset) {
    ShadowStore agent;
    EXPECT_THROW(agent.setDateTime(1, 1, 0.0), std::logic_error);
}

TEST(ShadowStoreUnregisteredTest, SetLightBeforeRegistrationThrowsBecauseContextIsUnset) {
    ShadowStore agent;
    EXPECT_THROW(agent.setLight(50.0), std::logic_error);
}

TEST(ShadowStoreUnregisteredTest, SetDarkBeforeRegistrationThrowsBecauseContextIsUnset) {
    ShadowStore agent;
    EXPECT_THROW(agent.setDark(50.0), std::logic_error);
}

// -- isAllDefault() / Restore API ----------------------------------------

TEST(ShadowStoreRestoreTest, ClearForRestoreResetsEveryFieldToDefault) {
    // set*() dispatches -- needs a registered context, same as every other
    // mutator here (see ShadowStoreUnregisteredTest above).
    Kernel kernel;
    kernel.registerAgent(std::make_shared<ShadowStore>());
    auto shadow = kernel.agentAs<ShadowStore>(kShadowStoreName);

    // OFF is the non-default value here -- true would no-op and fail the
    // ASSERT.
    ASSERT_TRUE(shadow->setUseSunForShading(false));
    ASSERT_TRUE(shadow->setShowShadows(true));
    ASSERT_TRUE(shadow->setPosition(51.48, -0.08));
    ASSERT_TRUE(shadow->setDateTime(6, 21, 9.0));
    ASSERT_TRUE(shadow->setLight(50.0));
    ASSERT_TRUE(shadow->setDark(20.0));
    ASSERT_FALSE(shadow->isAllDefault());

    shadow->clearForRestore();

    EXPECT_EQ(shadow->useSunForShading(), plnr::agent::kDefaultUseSunForShading);
    EXPECT_FALSE(shadow->showShadows());
    EXPECT_EQ(shadow->latitudeDeg(), kDefaultLatitudeDeg);
    EXPECT_EQ(shadow->longitudeDeg(), kDefaultLongitudeDeg);
    EXPECT_EQ(shadow->month(), kDefaultMonth);
    EXPECT_EQ(shadow->day(), kDefaultDay);
    EXPECT_EQ(shadow->hourLocal(), kDefaultHourLocal);
    EXPECT_EQ(shadow->light(), kDefaultLight);
    EXPECT_EQ(shadow->dark(), kDefaultDark);
    EXPECT_TRUE(shadow->isAllDefault());
}

// clearForRestore()/restoreXxx() are plain data manipulation -- no
// notification. Exercised without any kernel registration, proving they
// never reach Agent::send()/context().
TEST(ShadowStoreRestoreTest, RestoreApiDispatchesNothingAndNeedsNoRegistration) {
    ShadowStore agent;
    agent.restoreUseSunForShading(true);
    agent.restoreShowShadows(true);
    agent.restorePosition(51.48, -0.08);
    agent.restoreDateTime(6, 21, 9.0);
    agent.restoreLight(50.0);
    agent.restoreDark(20.0);

    EXPECT_TRUE(agent.useSunForShading());
    EXPECT_TRUE(agent.showShadows());
    EXPECT_EQ(agent.latitudeDeg(), 51.48);
    EXPECT_EQ(agent.longitudeDeg(), -0.08);
    EXPECT_EQ(agent.month(), 6);
    EXPECT_EQ(agent.day(), 21);
    EXPECT_EQ(agent.hourLocal(), 9.0);
    EXPECT_EQ(agent.light(), 50.0);
    EXPECT_EQ(agent.dark(), 20.0);
    EXPECT_FALSE(agent.isAllDefault());

    agent.clearForRestore();  // also dispatches nothing
    EXPECT_TRUE(agent.isAllDefault());
}

// isAllDefault() gates io::writeDocument's "omit the shadows key entirely"
// shortcut -- each field independently flips it false when non-default,
// true again once every field is back to default (not just "never touched").
TEST(ShadowStoreRestoreTest, IsAllDefaultReactsToEachFieldIndependently) {
    ShadowStore agent;
    EXPECT_TRUE(agent.isAllDefault());

    // useSunForShading's default is ON, so OFF is its non-default probe
    // value here.
    agent.restoreUseSunForShading(false);
    EXPECT_FALSE(agent.isAllDefault());
    agent.restoreUseSunForShading(plnr::agent::kDefaultUseSunForShading);
    EXPECT_TRUE(agent.isAllDefault());  // back to default -- true again, not "never touched"

    agent.restoreShowShadows(true);
    EXPECT_FALSE(agent.isAllDefault());
    agent.restoreShowShadows(false);
    EXPECT_TRUE(agent.isAllDefault());

    agent.restorePosition(0.0, 0.0);
    EXPECT_FALSE(agent.isAllDefault());
    agent.restorePosition(kDefaultLatitudeDeg, kDefaultLongitudeDeg);
    EXPECT_TRUE(agent.isAllDefault());

    agent.restoreDateTime(1, 1, 0.0);
    EXPECT_FALSE(agent.isAllDefault());
    agent.restoreDateTime(kDefaultMonth, kDefaultDay, kDefaultHourLocal);
    EXPECT_TRUE(agent.isAllDefault());

    agent.restoreLight(0.0);
    EXPECT_FALSE(agent.isAllDefault());
    agent.restoreLight(kDefaultLight);
    EXPECT_TRUE(agent.isAllDefault());

    agent.restoreDark(0.0);
    EXPECT_FALSE(agent.isAllDefault());
    agent.restoreDark(kDefaultDark);
    EXPECT_TRUE(agent.isAllDefault());
}

}  // namespace
