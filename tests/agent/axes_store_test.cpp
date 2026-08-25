#include "agent/axes_store.h"

#include <cmath>
#include <memory>
#include <stdexcept>

#include <ordo/core/app_kernel.h>

#include "agent/command/axes_commands.h"
#include "agent/events.h"

#include <geo/vec3.h>

#include <gtest/gtest.h>

namespace {

using ordo::core::AppKernel;
using plnr::agent::AxesStore;
using plnr::agent::kAxesStoreName;
using plnr::agent::ResetAxesCommand;
using plnr::agent::SetAxesCommand;
using plnr::events::AxesChanged;
using plnr::events::ResetAxesRequested;
using plnr::events::SetAxesRequested;
using plnr::geo::Vec3;

constexpr double kTol = 1e-9;

// Wires a kernel with a registered AxesStore plus both Axes commands, and
// an AxesChanged counter subscribed on the kernel's dispatcher for
// assertions.
class AxesChainTest : public ::testing::Test {
protected:
    void SetUp() override {
        kernel.registerAgent(std::make_shared<AxesStore>());
        kernel.registerCommand<SetAxesRequested, SetAxesCommand>();
        kernel.registerCommand<ResetAxesRequested, ResetAxesCommand>();
        kernel.dispatcher().subscribe<AxesChanged>(&changedCount, [this](const AxesChanged&) { ++changedCount; });
        axes = kernel.agentAs<AxesStore>(kAxesStoreName);
    }

    AppKernel kernel;
    std::shared_ptr<AxesStore> axes;
    int changedCount = 0;
};

TEST_F(AxesChainTest, StoreStartsAtWorldDefault) {
    ASSERT_NE(axes, nullptr);
    const plnr::agent::Frame& f = axes->frame();
    EXPECT_EQ(f.origin.x, 0.0);
    EXPECT_EQ(f.origin.y, 0.0);
    EXPECT_EQ(f.origin.z, 0.0);
    EXPECT_EQ(f.xDir.x, 1.0);
    EXPECT_EQ(f.yDir.y, 1.0);
    EXPECT_EQ(f.zDir.z, 1.0);
}

TEST_F(AxesChainTest, SetAxesRequestedChangesFrameAndFiresOnce) {
    kernel.send(SetAxesRequested{Vec3{1.0, 2.0, 3.0}, Vec3{0.0, 1.0, 0.0}, Vec3{1.0, 0.0, 0.0}});

    const plnr::agent::Frame& f = axes->frame();
    EXPECT_EQ(f.origin.x, 1.0);
    EXPECT_EQ(f.origin.y, 2.0);
    EXPECT_EQ(f.origin.z, 3.0);
    EXPECT_NEAR(f.xDir.x, 0.0, kTol);
    EXPECT_NEAR(f.xDir.y, 1.0, kTol);
    EXPECT_EQ(changedCount, 1);
}

// x = normalize(primary) = (0,1,0). z = normalize(cross(x, secondary)) =
// normalize(cross((0,1,0),(1,0,0))) = normalize((0,0,-1)) = (0,0,-1).
// y = cross(z, x) = cross((0,0,-1),(0,1,0)) = (1,0,0).
TEST_F(AxesChainTest, SetDerivesExactConstructionRule) {
    axes->set(Vec3{0, 0, 0}, Vec3{0.0, 1.0, 0.0}, Vec3{1.0, 0.0, 0.0});

    const plnr::agent::Frame& f = axes->frame();
    EXPECT_NEAR(f.xDir.x, 0.0, kTol);
    EXPECT_NEAR(f.xDir.y, 1.0, kTol);
    EXPECT_NEAR(f.xDir.z, 0.0, kTol);
    EXPECT_NEAR(f.zDir.x, 0.0, kTol);
    EXPECT_NEAR(f.zDir.y, 0.0, kTol);
    EXPECT_NEAR(f.zDir.z, -1.0, kTol);
    EXPECT_NEAR(f.yDir.x, 1.0, kTol);
    EXPECT_NEAR(f.yDir.y, 0.0, kTol);
    EXPECT_NEAR(f.yDir.z, 0.0, kTol);
}

TEST_F(AxesChainTest, SetOrthonormalizesNonUnitNonPerpendicularInput) {
    // primaryDir not unit (length 5); secondaryHint not perpendicular to it
    // either (has a component along primaryDir) -- the agent must still
    // produce a unit, mutually orthogonal result.
    axes->set(Vec3{0, 0, 0}, Vec3{5.0, 0.0, 0.0}, Vec3{3.0, 3.0, 0.0});

    const plnr::agent::Frame& f = axes->frame();
    auto len = [](const Vec3& v) { return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); };
    auto dot = [](const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; };

    EXPECT_NEAR(len(f.xDir), 1.0, kTol);
    EXPECT_NEAR(len(f.yDir), 1.0, kTol);
    EXPECT_NEAR(len(f.zDir), 1.0, kTol);
    EXPECT_NEAR(dot(f.xDir, f.yDir), 0.0, kTol);
    EXPECT_NEAR(dot(f.yDir, f.zDir), 0.0, kTol);
    EXPECT_NEAR(dot(f.zDir, f.xDir), 0.0, kTol);
    // Right-handed: xDir cross yDir == zDir.
    const Vec3 crossXY{f.xDir.y * f.yDir.z - f.xDir.z * f.yDir.y, f.xDir.z * f.yDir.x - f.xDir.x * f.yDir.z,
                        f.xDir.x * f.yDir.y - f.xDir.y * f.yDir.x};
    EXPECT_NEAR(crossXY.x, f.zDir.x, kTol);
    EXPECT_NEAR(crossXY.y, f.zDir.y, kTol);
    EXPECT_NEAR(crossXY.z, f.zDir.z, kTol);
}

TEST_F(AxesChainTest, SetWithDegeneratePrimaryIsANoOp) {
    const bool changed = axes->set(Vec3{0, 0, 0}, Vec3{0.0, 0.0, 0.0}, Vec3{1.0, 0.0, 0.0});

    EXPECT_FALSE(changed);
    EXPECT_EQ(changedCount, 0);
    EXPECT_EQ(axes->frame().xDir.x, 1.0);  // still world default
}

TEST_F(AxesChainTest, SetWithSecondaryParallelToPrimaryIsANoOp) {
    const bool changed = axes->set(Vec3{0, 0, 0}, Vec3{1.0, 0.0, 0.0}, Vec3{2.0, 0.0, 0.0});

    EXPECT_FALSE(changed);
    EXPECT_EQ(changedCount, 0);
}

TEST_F(AxesChainTest, SetWithSameResultingFrameIsANoOp) {
    axes->set(Vec3{1, 2, 3}, Vec3{0.0, 1.0, 0.0}, Vec3{1.0, 0.0, 0.0});
    changedCount = 0;

    // Re-derives the identical orthonormal result -- same origin/primary/
    // secondary.
    const bool changed = axes->set(Vec3{1, 2, 3}, Vec3{0.0, 1.0, 0.0}, Vec3{1.0, 0.0, 0.0});

    EXPECT_FALSE(changed);
    EXPECT_EQ(changedCount, 0);
}

TEST_F(AxesChainTest, ResetAxesRequestedReturnsToWorldDefaultAndFiresOnce) {
    axes->set(Vec3{1, 2, 3}, Vec3{0.0, 1.0, 0.0}, Vec3{1.0, 0.0, 0.0});
    changedCount = 0;

    kernel.send(ResetAxesRequested{});

    const plnr::agent::Frame& f = axes->frame();
    EXPECT_EQ(f.origin.x, 0.0);
    EXPECT_EQ(f.xDir.x, 1.0);
    EXPECT_EQ(f.yDir.y, 1.0);
    EXPECT_EQ(f.zDir.z, 1.0);
    EXPECT_EQ(changedCount, 1);
}

TEST_F(AxesChainTest, ResetWhenAlreadyDefaultIsANoOp) {
    const bool changed = axes->reset();

    EXPECT_FALSE(changed);
    EXPECT_EQ(changedCount, 0);
}

TEST(AxesStoreUnregisteredTest, SetBeforeRegistrationThrowsBecauseContextIsUnset) {
    AxesStore agent;
    // Must derive a frame DIFFERENT from the world-default initial frame_,
    // or set() short-circuits on "no-op: already equal" before ever
    // touching context()/send() -- hence the non-default primary/secondary here.
    EXPECT_THROW(agent.set(Vec3{0, 0, 0}, Vec3{0, 1, 0}, Vec3{1, 0, 0}), std::logic_error);
}

}  // namespace
