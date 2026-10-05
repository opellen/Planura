#include "agent/section_store.h"

#include <memory>
#include <stdexcept>

#include <ordo/core/kernel.h>

#include "agent/events.h"
#include "agent/command/section_commands.h"

#include <geo/vec3.h>

#include <gtest/gtest.h>

namespace {

using ordo::core::Kernel;
using plnr::agent::AddSectionPlaneCommand;
using plnr::agent::kSectionStoreName;
using plnr::agent::RemoveSectionPlaneCommand;
using plnr::agent::ReverseSectionCommand;
using plnr::agent::SectionPlane;
using plnr::agent::SectionStore;
using plnr::agent::SetSectionActiveCommand;
using plnr::agent::SetSectionHiddenCommand;
using plnr::events::AddSectionPlaneRequested;
using plnr::events::RemoveSectionPlaneRequested;
using plnr::events::ReverseSectionRequested;
using plnr::events::SectionsChanged;
using plnr::events::SetSectionActiveRequested;
using plnr::events::SetSectionHiddenRequested;
using plnr::geo::Vec3;

// Wires a kernel with a registered SectionStore plus all 5 section
// commands, and a SectionsChanged counter on the kernel's dispatcher for
// assertions.
class SectionChainTest : public ::testing::Test {
protected:
    void SetUp() override {
        kernel.registerAgent(std::make_shared<SectionStore>());
        kernel.registerCommand<AddSectionPlaneRequested, AddSectionPlaneCommand>();
        kernel.registerCommand<RemoveSectionPlaneRequested, RemoveSectionPlaneCommand>();
        kernel.registerCommand<SetSectionActiveRequested, SetSectionActiveCommand>();
        kernel.registerCommand<ReverseSectionRequested, ReverseSectionCommand>();
        kernel.registerCommand<SetSectionHiddenRequested, SetSectionHiddenCommand>();
        kernel.dispatcher().subscribe<SectionsChanged>(&changedCount, [this](const SectionsChanged&) { ++changedCount; });
        sections = kernel.agentAs<SectionStore>(kSectionStoreName);
    }

    Kernel kernel;
    std::shared_ptr<SectionStore> sections;
    int changedCount = 0;
};

TEST_F(SectionChainTest, StoreStartsEmpty) {
    ASSERT_NE(sections, nullptr);
    EXPECT_TRUE(sections->planes().empty());
    EXPECT_EQ(sections->activePlane(), nullptr);
}

TEST_F(SectionChainTest, AddPlaneNormalizesNormalAndFiresOnce) {
    const plnr::geo::Id id = sections->addPlane(Vec3{1.0, 2.0, 3.0}, Vec3{0.0, 0.0, 2.0}, "");

    ASSERT_EQ(sections->planes().size(), 1u);
    const SectionPlane& p = sections->planes().front();
    EXPECT_EQ(p.id, id);
    EXPECT_EQ(p.point.x, 1.0);
    EXPECT_NEAR(p.normal.z, 1.0, 1e-9);  // normalized from {0,0,2}
    EXPECT_FALSE(p.active);
    EXPECT_FALSE(p.hidden);
    EXPECT_EQ(changedCount, 1);
}

TEST_F(SectionChainTest, AddPlaneWithEmptyNameAutoNames) {
    const plnr::geo::Id id = sections->addPlane(Vec3{0, 0, 0}, Vec3{0, 0, 1}, "");

    EXPECT_EQ(sections->planes().front().name, "Section Plane " + std::to_string(id));
}

TEST_F(SectionChainTest, AddPlaneWithExplicitNameKeepsIt) {
    sections->addPlane(Vec3{0, 0, 0}, Vec3{0, 0, 1}, "My Plane");

    EXPECT_EQ(sections->planes().front().name, "My Plane");
}

TEST_F(SectionChainTest, AddPlaneReturnsIncreasingIds) {
    const plnr::geo::Id first = sections->addPlane({0, 0, 0}, {0, 0, 1}, "");
    const plnr::geo::Id second = sections->addPlane({0, 0, 0}, {1, 0, 0}, "");

    EXPECT_NE(first, plnr::geo::kInvalidId);
    EXPECT_NE(second, plnr::geo::kInvalidId);
    EXPECT_NE(first, second);
}

TEST_F(SectionChainTest, AddSectionPlaneRequestedFiresOnce) {
    kernel.send(AddSectionPlaneRequested{Vec3{4.0, 5.0, 6.0}, Vec3{0.0, 1.0, 0.0}, "Named"});

    ASSERT_EQ(sections->planes().size(), 1u);
    EXPECT_EQ(sections->planes().front().name, "Named");
    EXPECT_EQ(changedCount, 1);
}

TEST_F(SectionChainTest, RemovePlaneRemovesItAndFiresOnce) {
    const plnr::geo::Id id = sections->addPlane({0, 0, 0}, {0, 0, 1}, "");
    changedCount = 0;

    kernel.send(RemoveSectionPlaneRequested{id});

    EXPECT_TRUE(sections->planes().empty());
    EXPECT_EQ(changedCount, 1);
}

TEST_F(SectionChainTest, RemovePlaneWithUnknownIdIsANoOp) {
    sections->addPlane({0, 0, 0}, {0, 0, 1}, "");
    changedCount = 0;

    kernel.send(RemoveSectionPlaneRequested{424242});

    EXPECT_EQ(sections->planes().size(), 1u);
    EXPECT_EQ(changedCount, 0);
}

TEST_F(SectionChainTest, RemovingActivePlaneClearsTheCutWithoutPromotingAnother) {
    const plnr::geo::Id a = sections->addPlane({0, 0, 0}, {0, 0, 1}, "");
    const plnr::geo::Id b = sections->addPlane({1, 0, 0}, {1, 0, 0}, "");
    sections->setActive(a, true);
    changedCount = 0;

    sections->removePlane(a);

    ASSERT_EQ(sections->planes().size(), 1u);
    EXPECT_EQ(sections->planes().front().id, b);
    EXPECT_EQ(sections->activePlane(), nullptr);  // b is NOT auto-promoted
}

TEST_F(SectionChainTest, SetActiveTrueActivatesAndFiresOnce) {
    const plnr::geo::Id id = sections->addPlane({0, 0, 0}, {0, 0, 1}, "");
    changedCount = 0;

    kernel.send(SetSectionActiveRequested{id, true});

    ASSERT_NE(sections->activePlane(), nullptr);
    EXPECT_EQ(sections->activePlane()->id, id);
    EXPECT_EQ(changedCount, 1);
}

TEST_F(SectionChainTest, SingleActiveCutInvariantDeactivatesPreviousAndFiresOnce) {
    const plnr::geo::Id a = sections->addPlane({0, 0, 0}, {0, 0, 1}, "");
    const plnr::geo::Id b = sections->addPlane({1, 0, 0}, {1, 0, 0}, "");
    sections->setActive(a, true);
    ASSERT_TRUE(sections->activePlane() != nullptr && sections->activePlane()->id == a);
    changedCount = 0;

    // Activating b must deactivate a, coalesced into ONE SectionsChanged.
    const bool changed = sections->setActive(b, true);

    EXPECT_TRUE(changed);
    ASSERT_NE(sections->activePlane(), nullptr);
    EXPECT_EQ(sections->activePlane()->id, b);
    for (const SectionPlane& p : sections->planes()) {
        if (p.id == a) EXPECT_FALSE(p.active);
    }
    EXPECT_EQ(changedCount, 1);
}

TEST_F(SectionChainTest, SetActiveTrueOnAlreadyActivePlaneIsANoOp) {
    const plnr::geo::Id id = sections->addPlane({0, 0, 0}, {0, 0, 1}, "");
    sections->setActive(id, true);
    changedCount = 0;

    const bool changed = sections->setActive(id, true);

    EXPECT_FALSE(changed);
    EXPECT_EQ(changedCount, 0);
}

TEST_F(SectionChainTest, SetActiveFalseDeactivatesWithoutActivatingAnother) {
    const plnr::geo::Id a = sections->addPlane({0, 0, 0}, {0, 0, 1}, "");
    sections->addPlane({1, 0, 0}, {1, 0, 0}, "");
    sections->setActive(a, true);
    changedCount = 0;

    kernel.send(SetSectionActiveRequested{a, false});

    EXPECT_EQ(sections->activePlane(), nullptr);
    EXPECT_EQ(changedCount, 1);
}

TEST_F(SectionChainTest, SetActiveFalseOnAlreadyInactivePlaneIsANoOp) {
    const plnr::geo::Id id = sections->addPlane({0, 0, 0}, {0, 0, 1}, "");
    changedCount = 0;

    const bool changed = sections->setActive(id, false);

    EXPECT_FALSE(changed);
    EXPECT_EQ(changedCount, 0);
}

TEST_F(SectionChainTest, SetActiveWithUnknownIdIsANoOp) {
    kernel.send(SetSectionActiveRequested{424242, true});

    EXPECT_EQ(changedCount, 0);
    EXPECT_EQ(sections->activePlane(), nullptr);
}

TEST_F(SectionChainTest, ReverseNegatesNormalAndFiresOnce) {
    const plnr::geo::Id id = sections->addPlane({0, 0, 0}, {1.0, 0.0, 0.0}, "");
    changedCount = 0;

    kernel.send(ReverseSectionRequested{id});

    EXPECT_NEAR(sections->planes().front().normal.x, -1.0, 1e-9);
    EXPECT_EQ(changedCount, 1);
}

TEST_F(SectionChainTest, ReverseWithUnknownIdIsANoOp) {
    kernel.send(ReverseSectionRequested{424242});

    EXPECT_EQ(changedCount, 0);
}

TEST_F(SectionChainTest, SetHiddenTrueHidesAndFiresOnce) {
    const plnr::geo::Id id = sections->addPlane({0, 0, 0}, {0, 0, 1}, "");
    changedCount = 0;

    kernel.send(SetSectionHiddenRequested{id, true});

    EXPECT_TRUE(sections->planes().front().hidden);
    EXPECT_EQ(changedCount, 1);
}

TEST_F(SectionChainTest, SetHiddenToSameValueIsANoOp) {
    const plnr::geo::Id id = sections->addPlane({0, 0, 0}, {0, 0, 1}, "");
    changedCount = 0;

    kernel.send(SetSectionHiddenRequested{id, false});  // already visible

    EXPECT_FALSE(sections->planes().front().hidden);
    EXPECT_EQ(changedCount, 0);
}

TEST_F(SectionChainTest, SetHiddenWithUnknownIdIsANoOp) {
    kernel.send(SetSectionHiddenRequested{424242, true});

    EXPECT_EQ(changedCount, 0);
}

TEST_F(SectionChainTest, HiddenIsOrthogonalToActive) {
    const plnr::geo::Id id = sections->addPlane({0, 0, 0}, {0, 0, 1}, "");
    sections->setActive(id, true);

    sections->setHidden(id, true);

    ASSERT_NE(sections->activePlane(), nullptr);
    EXPECT_EQ(sections->activePlane()->id, id);
    EXPECT_TRUE(sections->planes().front().hidden);
}

TEST(SectionStoreUnregisteredTest, AddPlaneBeforeRegistrationThrowsBecauseContextIsUnset) {
    SectionStore agent;
    EXPECT_THROW(agent.addPlane({0, 0, 0}, {0, 0, 1}, ""), std::logic_error);
}

TEST(SectionStoreUnregisteredTest, RemoveOnEmptyUnregisteredStoreDoesNotThrow) {
    // removePlane() on an unknown id sends nothing, so it never touches
    // context() -- no agent registration required, mirroring
    // GuideStoreUnregisteredTest::EraseOnEmptyUnregisteredStoreDoesNotThrow.
    SectionStore agent;
    EXPECT_NO_THROW(EXPECT_FALSE(agent.removePlane(1)));
}

}  // namespace
