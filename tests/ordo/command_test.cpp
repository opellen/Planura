#include <ordo/core/command.h>

#include <ordo/core/app_kernel.h>
#include <ordo/core/agent.h>

#include <memory>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

namespace {

using ordo::core::AppKernel;
using ordo::core::Command;
using ordo::core::Agent;

// Event: request to rename the active document.
struct RenameDocumentEvent {
    static constexpr std::string_view eventName = "RenameDocument";
    std::string newName;
};

// Event: request to switch the active tool. A distinct type from
// RenameDocumentEvent, so the two commands below can be proven independent.
struct SelectToolEvent {
    static constexpr std::string_view eventName = "SelectTool";
    std::string toolId;
};

// An agent used purely to observe command side effects, instead of relying on
// global test state. Commands look this up on the kernel they were executed
// against, which also exercises the Command -> AppKernel -> Agent path.
class ResultsStore : public Agent {
public:
    static constexpr const char* kName = "ResultsStore";

    ResultsStore() : Agent(kName) {}

    std::string lastRenameSeenByV1;
    int renameV1ExecuteCount = 0;
    std::string lastRenameSeenByV2;
    std::string lastToolSelected;
};

class RenameDocumentCommand : public Command<RenameDocumentEvent> {
public:
    void execute(AppKernel& kernel, const RenameDocumentEvent& event) override {
        auto results = kernel.agentAs<ResultsStore>(ResultsStore::kName);
        results->lastRenameSeenByV1 = event.newName;
        ++results->renameV1ExecuteCount;
    }
};

// A second implementation for the same event, used to prove that
// re-registering a command replaces the previously registered factory.
class RenameDocumentCommandV2 : public Command<RenameDocumentEvent> {
public:
    void execute(AppKernel& kernel, const RenameDocumentEvent& event) override {
        auto results = kernel.agentAs<ResultsStore>(ResultsStore::kName);
        results->lastRenameSeenByV2 = event.newName;
    }
};

class SelectToolCommand : public Command<SelectToolEvent> {
public:
    void execute(AppKernel& kernel, const SelectToolEvent& event) override {
        auto results = kernel.agentAs<ResultsStore>(ResultsStore::kName);
        results->lastToolSelected = event.toolId;
    }
};

class CommandTest : public ::testing::Test {
protected:
    void SetUp() override {
        kernel.registerAgent(std::make_shared<ResultsStore>());
        results = kernel.agentAs<ResultsStore>(ResultsStore::kName);
    }

    AppKernel kernel;
    std::shared_ptr<ResultsStore> results;
};

TEST_F(CommandTest, DispatchedEventCreatesAndExecutesCommandWithTypedPayload) {
    kernel.registerCommand<RenameDocumentEvent, RenameDocumentCommand>();

    kernel.send(RenameDocumentEvent{"Deck.plr"});

    EXPECT_EQ(results->lastRenameSeenByV1, "Deck.plr");
    EXPECT_EQ(results->renameV1ExecuteCount, 1);
}

TEST_F(CommandTest, ReRegistrationReplacesThePreviousFactory) {
    kernel.registerCommand<RenameDocumentEvent, RenameDocumentCommand>();
    kernel.registerCommand<RenameDocumentEvent, RenameDocumentCommandV2>();

    kernel.send(RenameDocumentEvent{"Roof.plr"});

    EXPECT_TRUE(results->lastRenameSeenByV1.empty());     // old factory no longer runs
    EXPECT_EQ(results->lastRenameSeenByV2, "Roof.plr");    // new factory ran instead
}

TEST_F(CommandTest, RemoveCommandStopsExecution) {
    kernel.registerCommand<RenameDocumentEvent, RenameDocumentCommand>();
    kernel.removeCommand<RenameDocumentEvent>();

    kernel.send(RenameDocumentEvent{"Wall.plr"});

    EXPECT_EQ(results->renameV1ExecuteCount, 0);
}

TEST_F(CommandTest, TwoDifferentEventsWithTwoDifferentCommandsWorkIndependently) {
    kernel.registerCommand<RenameDocumentEvent, RenameDocumentCommand>();
    kernel.registerCommand<SelectToolEvent, SelectToolCommand>();

    kernel.send(SelectToolEvent{"eraser"});

    EXPECT_EQ(results->lastToolSelected, "eraser");
    EXPECT_TRUE(results->lastRenameSeenByV1.empty());  // rename command untouched

    kernel.removeCommand<SelectToolEvent>();
    kernel.send(RenameDocumentEvent{"Beam.plr"});

    EXPECT_EQ(results->lastRenameSeenByV1, "Beam.plr");  // rename command still works
}

}  // namespace
