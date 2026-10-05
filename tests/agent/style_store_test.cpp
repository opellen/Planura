#include "agent/style_store.h"

#include <memory>
#include <stdexcept>

#include <ordo/core/kernel.h>

#include "agent/events.h"
#include "agent/command/style_commands.h"

#include <gtest/gtest.h>

namespace {

using ordo::core::Kernel;
using plnr::agent::kStyleStoreName;
using plnr::agent::SetAmbientOcclusionCommand;
using plnr::agent::SetAoStrengthCommand;
using plnr::agent::SetEdgeStyleFlagCommand;
using plnr::agent::SetFaceStyleCommand;
using plnr::agent::StyleColor;
using plnr::agent::StyleStore;
using plnr::events::EdgeFlag;
using plnr::events::FaceStyle;
using plnr::events::SetAmbientOcclusionRequested;
using plnr::events::SetAoStrengthRequested;
using plnr::events::SetEdgeStyleFlagRequested;
using plnr::events::SetFaceStyleRequested;
using plnr::events::StyleChanged;

// -- Direct agent-method tests (no kernel needed except where set*() must
// dispatch) ------------------------------------------------------------

TEST(StyleStoreDefaultsTest, StartsAtShadedWithTexturesDefaultFlagsDefaultColors) {
    StyleStore agent;
    EXPECT_EQ(agent.faceStyle(), FaceStyle::ShadedWithTextures);
    // Profiles defaults ON (the reference modeler's own default style ships with it on);
    // depthCue/backEdges stay off.
    EXPECT_EQ(agent.profiles(), plnr::agent::kDefaultProfiles);
    EXPECT_FALSE(agent.depthCue());
    EXPECT_FALSE(agent.backEdges());
    EXPECT_EQ(agent.defaultFrontColor().r, plnr::agent::kDefaultFrontColorR);
    EXPECT_EQ(agent.defaultFrontColor().g, plnr::agent::kDefaultFrontColorG);
    EXPECT_EQ(agent.defaultFrontColor().b, plnr::agent::kDefaultFrontColorB);
    EXPECT_EQ(agent.defaultBackColor().r, plnr::agent::kDefaultBackColorR);
    EXPECT_EQ(agent.defaultBackColor().g, plnr::agent::kDefaultBackColorG);
    EXPECT_EQ(agent.defaultBackColor().b, plnr::agent::kDefaultBackColorB);
    EXPECT_FALSE(agent.ambientOcclusion());
    EXPECT_EQ(agent.aoStrength(), plnr::agent::kDefaultAoStrength);
    EXPECT_TRUE(agent.isAllDefault());
}

// Wires a fresh kernel with StyleStore + both style commands, plus a
// StyleChanged counter on the dispatcher.
class StyleChainTest : public ::testing::Test {
protected:
    void SetUp() override {
        kernel.registerAgent(std::make_shared<StyleStore>());
        kernel.registerCommand<SetFaceStyleRequested, SetFaceStyleCommand>();
        kernel.registerCommand<SetEdgeStyleFlagRequested, SetEdgeStyleFlagCommand>();
        kernel.registerCommand<SetAmbientOcclusionRequested, SetAmbientOcclusionCommand>();
        kernel.registerCommand<SetAoStrengthRequested, SetAoStrengthCommand>();
        kernel.dispatcher().subscribe<StyleChanged>(&changedCount, [this](const StyleChanged&) { ++changedCount; });
        style = kernel.agentAs<StyleStore>(kStyleStoreName);
    }

    Kernel kernel;
    std::shared_ptr<StyleStore> style;
    int changedCount = 0;
};

TEST_F(StyleChainTest, SetFaceStyleRequestedChangesStyleAndFiresOnce) {
    ASSERT_NE(style, nullptr);
    kernel.send(SetFaceStyleRequested{FaceStyle::Wireframe});
    EXPECT_EQ(style->faceStyle(), FaceStyle::Wireframe);
    EXPECT_EQ(changedCount, 1);
    EXPECT_FALSE(style->isAllDefault());
}

TEST_F(StyleChainTest, SetFaceStyleRequestedToSameValueIsANoOp) {
    kernel.send(SetFaceStyleRequested{FaceStyle::ShadedWithTextures});  // already the default
    EXPECT_EQ(changedCount, 0);
}

// Every FaceStyle enumerator round-trips through the event/command path --
// guards against a value silently falling through a switch somewhere along
// the chain (Agent, .plr writer/reader, bridge) as new modes were added.
TEST_F(StyleChainTest, EverySingleFaceStyleValueIsReachableAndDistinct) {
    const FaceStyle styles[] = {FaceStyle::Wireframe,          FaceStyle::HiddenLine, FaceStyle::Shaded,
                                 FaceStyle::ShadedWithTextures, FaceStyle::Monochrome, FaceStyle::XRay};
    for (FaceStyle s : styles) {
        kernel.send(SetFaceStyleRequested{s});
        EXPECT_EQ(style->faceStyle(), s);
    }
}

TEST_F(StyleChainTest, SetEdgeStyleFlagRequestedChangesOneFlagAndFiresOnce) {
    // Profiles defaults ON -- toggling it OFF is the change that must fire,
    // and what flips isAllDefault() false here.
    kernel.send(SetEdgeStyleFlagRequested{EdgeFlag::Profiles, false});
    EXPECT_FALSE(style->profiles());
    EXPECT_FALSE(style->depthCue());
    EXPECT_FALSE(style->backEdges());
    EXPECT_EQ(changedCount, 1);
    EXPECT_FALSE(style->isAllDefault());

    kernel.send(SetEdgeStyleFlagRequested{EdgeFlag::DepthCue, true});
    EXPECT_FALSE(style->profiles());  // unaffected by the DepthCue toggle
    EXPECT_TRUE(style->depthCue());
    EXPECT_EQ(changedCount, 2);

    kernel.send(SetEdgeStyleFlagRequested{EdgeFlag::BackEdges, true});
    EXPECT_TRUE(style->backEdges());
    EXPECT_EQ(changedCount, 3);
}

TEST_F(StyleChainTest, SetEdgeStyleFlagRequestedToSameValueIsANoOp) {
    // Profiles defaults ON, so the no-op probe starts from true rather than
    // false.
    kernel.send(SetEdgeStyleFlagRequested{EdgeFlag::Profiles, true});  // already true
    EXPECT_EQ(changedCount, 0);

    kernel.send(SetEdgeStyleFlagRequested{EdgeFlag::Profiles, false});
    ASSERT_EQ(changedCount, 1);
    kernel.send(SetEdgeStyleFlagRequested{EdgeFlag::Profiles, false});  // already false now
    EXPECT_EQ(changedCount, 1);
}

TEST_F(StyleChainTest, SetAmbientOcclusionRequestedTogglesAndFiresOnce) {
    kernel.send(SetAmbientOcclusionRequested{true});
    EXPECT_TRUE(style->ambientOcclusion());
    EXPECT_EQ(changedCount, 1);
    EXPECT_FALSE(style->isAllDefault());

    kernel.send(SetAmbientOcclusionRequested{false});
    EXPECT_FALSE(style->ambientOcclusion());
    EXPECT_EQ(changedCount, 2);
    EXPECT_TRUE(style->isAllDefault());
}

TEST_F(StyleChainTest, SetAmbientOcclusionRequestedToSameValueIsANoOp) {
    kernel.send(SetAmbientOcclusionRequested{false});  // already false
    EXPECT_EQ(changedCount, 0);
}

TEST_F(StyleChainTest, SetAoStrengthRequestedChangesValueAndFiresOnce) {
    ASSERT_EQ(style->aoStrength(), plnr::agent::kDefaultAoStrength);
    kernel.send(SetAoStrengthRequested{0.3});
    EXPECT_EQ(style->aoStrength(), 0.3);
    EXPECT_EQ(changedCount, 1);
    EXPECT_FALSE(style->isAllDefault());
}

TEST_F(StyleChainTest, SetAoStrengthRequestedToSameValueIsANoOp) {
    kernel.send(SetAoStrengthRequested{plnr::agent::kDefaultAoStrength});  // already the default
    EXPECT_EQ(changedCount, 0);
}

TEST(StyleStoreUnregisteredTest, SetFaceStyleBeforeRegistrationThrowsBecauseContextIsUnset) {
    StyleStore agent;
    EXPECT_THROW(agent.setFaceStyle(FaceStyle::Wireframe), std::logic_error);
}

TEST(StyleStoreUnregisteredTest, SetEdgeFlagBeforeRegistrationThrowsBecauseContextIsUnset) {
    StyleStore agent;
    // false is the non-default value here -- true would no-op before
    // reaching the throwing send(), and EXPECT_THROW would fail.
    EXPECT_THROW(agent.setEdgeFlag(EdgeFlag::Profiles, false), std::logic_error);
}

TEST(StyleStoreUnregisteredTest, SetAmbientOcclusionBeforeRegistrationThrowsBecauseContextIsUnset) {
    StyleStore agent;
    EXPECT_THROW(agent.setAmbientOcclusion(true), std::logic_error);
}

TEST(StyleStoreUnregisteredTest, SetAoStrengthBeforeRegistrationThrowsBecauseContextIsUnset) {
    StyleStore agent;
    EXPECT_THROW(agent.setAoStrength(0.3), std::logic_error);
}

// -- isAllDefault() / Restore API ----------------------------------------

TEST(StyleStoreRestoreTest, ClearForRestoreResetsEveryFieldToDefault) {
    // set*() dispatches -- needs a registered context, same as every other
    // mutator here (see StyleStoreUnregisteredTest above).
    Kernel kernel;
    kernel.registerAgent(std::make_shared<StyleStore>());
    auto style = kernel.agentAs<StyleStore>(kStyleStoreName);

    ASSERT_TRUE(style->setFaceStyle(FaceStyle::XRay));
    // Profiles defaults ON -- OFF is the non-default value this test needs
    // clearForRestore() to undo.
    ASSERT_TRUE(style->setEdgeFlag(EdgeFlag::Profiles, false));
    ASSERT_TRUE(style->setEdgeFlag(EdgeFlag::DepthCue, true));
    ASSERT_TRUE(style->setEdgeFlag(EdgeFlag::BackEdges, true));
    ASSERT_TRUE(style->setAmbientOcclusion(true));
    ASSERT_TRUE(style->setAoStrength(0.2));
    style->restoreColors(StyleColor{0.1, 0.2, 0.3}, StyleColor{0.4, 0.5, 0.6});
    ASSERT_FALSE(style->isAllDefault());

    style->clearForRestore();

    EXPECT_EQ(style->faceStyle(), FaceStyle::ShadedWithTextures);
    EXPECT_EQ(style->profiles(), plnr::agent::kDefaultProfiles);
    EXPECT_FALSE(style->depthCue());
    EXPECT_FALSE(style->backEdges());
    EXPECT_FALSE(style->ambientOcclusion());
    EXPECT_EQ(style->aoStrength(), plnr::agent::kDefaultAoStrength);
    EXPECT_EQ(style->defaultFrontColor().r, plnr::agent::kDefaultFrontColorR);
    EXPECT_EQ(style->defaultBackColor().r, plnr::agent::kDefaultBackColorR);
    EXPECT_TRUE(style->isAllDefault());
}

// clearForRestore()/restoreXxx() are plain data manipulation -- no
// notification. Exercised without any kernel registration, proving they
// never reach Agent::send()/context().
TEST(StyleStoreRestoreTest, RestoreApiDispatchesNothingAndNeedsNoRegistration) {
    StyleStore agent;
    agent.restoreFaceStyle(FaceStyle::Monochrome);
    agent.restoreEdgeFlags(true, false, true);
    agent.restoreAmbientOcclusion(true, 0.4);
    agent.restoreColors(StyleColor{0.9, 0.8, 0.7}, StyleColor{0.1, 0.1, 0.1});

    EXPECT_EQ(agent.faceStyle(), FaceStyle::Monochrome);
    EXPECT_TRUE(agent.profiles());
    EXPECT_FALSE(agent.depthCue());
    EXPECT_TRUE(agent.backEdges());
    EXPECT_TRUE(agent.ambientOcclusion());
    EXPECT_EQ(agent.aoStrength(), 0.4);
    EXPECT_EQ(agent.defaultFrontColor().r, 0.9);
    EXPECT_EQ(agent.defaultBackColor().b, 0.1);
    EXPECT_FALSE(agent.isAllDefault());

    agent.clearForRestore();  // also dispatches nothing
    EXPECT_TRUE(agent.isAllDefault());
}

// isAllDefault() gates io::writeDocument's "omit the style key entirely"
// shortcut -- each field independently flips it false when non-default,
// true again once every field is back to default (not just "never touched").
TEST(StyleStoreRestoreTest, IsAllDefaultReactsToEachFieldIndependently) {
    StyleStore agent;
    EXPECT_TRUE(agent.isAllDefault());

    agent.restoreFaceStyle(FaceStyle::Shaded);
    EXPECT_FALSE(agent.isAllDefault());
    agent.restoreFaceStyle(FaceStyle::ShadedWithTextures);
    EXPECT_TRUE(agent.isAllDefault());  // back to default -- true again, not "never touched"

    // Profiles' default is ON, so OFF is its non-default probe value here.
    agent.restoreEdgeFlags(false, false, false);
    EXPECT_FALSE(agent.isAllDefault());
    agent.restoreEdgeFlags(plnr::agent::kDefaultProfiles, false, false);
    EXPECT_TRUE(agent.isAllDefault());

    agent.restoreAmbientOcclusion(true, plnr::agent::kDefaultAoStrength);
    EXPECT_FALSE(agent.isAllDefault());
    agent.restoreAmbientOcclusion(false, plnr::agent::kDefaultAoStrength);
    EXPECT_TRUE(agent.isAllDefault());

    agent.restoreAmbientOcclusion(false, 0.2);
    EXPECT_FALSE(agent.isAllDefault());  // strength alone, independent of the toggle
    agent.restoreAmbientOcclusion(false, plnr::agent::kDefaultAoStrength);
    EXPECT_TRUE(agent.isAllDefault());

    const StyleColor defaultFront{plnr::agent::kDefaultFrontColorR, plnr::agent::kDefaultFrontColorG,
                                   plnr::agent::kDefaultFrontColorB};
    const StyleColor defaultBack{plnr::agent::kDefaultBackColorR, plnr::agent::kDefaultBackColorG,
                                  plnr::agent::kDefaultBackColorB};
    agent.restoreColors(StyleColor{0.0, 0.0, 0.0}, defaultBack);
    EXPECT_FALSE(agent.isAllDefault());
    agent.restoreColors(defaultFront, defaultBack);
    EXPECT_TRUE(agent.isAllDefault());
}

}  // namespace
