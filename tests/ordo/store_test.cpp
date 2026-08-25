#include <ordo/core/agent.h>

#include <ordo/core/app_kernel.h>
#include <ordo/core/agent_context.h>

#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

namespace {

using ordo::core::AppKernel;
using ordo::core::Agent;
using ordo::core::AgentContext;

// A toy domain agent tracking a document title, standing in for the kind of
// named data holder real Ordo agents will be (e.g. a SceneStore, ToolStore).
class TitleStore : public Agent {
public:
    static constexpr const char* kName = "TitleStore";

    TitleStore() : Agent(kName) {}

    void onRemove() override {
        removed = true;
    }

    bool removed = false;
    std::string title = "Untitled";
};

// Exposes the protected context() accessor so tests can probe its
// registration-state contract (throws outside the registered window).
class ContextProbeStore : public Agent {
public:
    explicit ContextProbeStore(std::string name) : Agent(std::move(name)) {}

    void onRemove() override {
        removed = true;
    }

    AgentContext& probeContext() const { return context(); }

    bool removed = false;
};

// Event used to prove an Agent's send() capability actually reaches the
// kernel's dispatcher, both during and after onRegister().
struct PingEvent {
    static constexpr std::string_view eventName = "Ping";
    int value = 0;
};

class AnnouncingStore : public Agent {
public:
    static constexpr const char* kName = "AnnouncingStore";

    AnnouncingStore() : Agent(kName) {}

    // context() (and therefore send()) must already work here: registration
    // has not returned yet.
    void onRegister() override { send(PingEvent{1}); }

    // Exercises send() well after registration has completed.
    void pingAgain(int value) { send(PingEvent{value}); }
};

// A sibling agent looked up through AgentContext rather than AppKernel.
class CounterStore : public Agent {
public:
    static constexpr const char* kName = "CounterStore";

    CounterStore() : Agent(kName) {}

    int value = 7;
};

class LookupStore : public Agent {
public:
    static constexpr const char* kName = "LookupStore";

    LookupStore() : Agent(kName) {}

    // Exercises sibling lookup through the narrow AgentContext, not AppKernel.
    std::shared_ptr<CounterStore> findCounter() const {
        return context().agentAs<CounterStore>(CounterStore::kName);
    }
};

TEST(StoreTest, RegisterRetrieveByNameThenRemove) {
    AppKernel kernel;
    auto agent = std::make_shared<TitleStore>();

    kernel.registerAgent(agent);

    auto found = kernel.agent(TitleStore::kName);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->name(), TitleStore::kName);

    EXPECT_TRUE(kernel.removeAgent(TitleStore::kName));
    EXPECT_EQ(kernel.agent(TitleStore::kName), nullptr);
}

TEST(StoreTest, OnRegisterCanSendBecauseContextIsAlreadyAvailable) {
    AppKernel kernel;
    std::vector<int> received;
    kernel.dispatcher().subscribe<PingEvent>(&received,
                                              [&received](const PingEvent& e) { received.push_back(e.value); });

    kernel.registerAgent(std::make_shared<AnnouncingStore>());

    ASSERT_EQ(received.size(), 1u);
    EXPECT_EQ(received[0], 1);
}

TEST(StoreTest, StoreCanSendAfterRegistrationCompletes) {
    AppKernel kernel;
    std::vector<int> received;
    kernel.dispatcher().subscribe<PingEvent>(&received,
                                              [&received](const PingEvent& e) { received.push_back(e.value); });

    auto agent = std::make_shared<AnnouncingStore>();
    kernel.registerAgent(agent);  // sends PingEvent{1} from onRegister

    agent->pingAgain(2);

    ASSERT_EQ(received.size(), 2u);
    EXPECT_EQ(received[1], 2);
}

TEST(StoreTest, StoreCanLookUpASiblingStoreThroughContext) {
    AppKernel kernel;
    kernel.registerAgent(std::make_shared<CounterStore>());
    auto lookup = std::make_shared<LookupStore>();
    kernel.registerAgent(lookup);

    auto counter = lookup->findCounter();
    ASSERT_NE(counter, nullptr);
    EXPECT_EQ(counter->value, 7);
}

TEST(StoreTest, ContextThrowsBeforeRegistrationAndAfterRemoval) {
    auto agent = std::make_shared<ContextProbeStore>("Probe");
    EXPECT_THROW(agent->probeContext(), std::logic_error);

    AppKernel kernel;
    kernel.registerAgent(agent);
    EXPECT_NO_THROW(agent->probeContext());

    kernel.removeAgent("Probe");
    EXPECT_THROW(agent->probeContext(), std::logic_error);
}

TEST(StoreTest, OnRemoveFiresOnRemoval) {
    AppKernel kernel;
    auto agent = std::make_shared<TitleStore>();
    kernel.registerAgent(agent);

    EXPECT_FALSE(agent->removed);
    kernel.removeAgent(TitleStore::kName);
    EXPECT_TRUE(agent->removed);
}

TEST(StoreTest, StoreAsDowncastsToConcreteType) {
    AppKernel kernel;
    auto agent = std::make_shared<TitleStore>();
    kernel.registerAgent(agent);

    auto typed = kernel.agentAs<TitleStore>(TitleStore::kName);
    ASSERT_NE(typed, nullptr);
    EXPECT_EQ(typed->title, "Untitled");
}

TEST(StoreTest, RemoveStoreOnMissingNameReturnsFalse) {
    AppKernel kernel;
    EXPECT_FALSE(kernel.removeAgent("DoesNotExist"));
}

TEST(StoreTest, ReplacingAStoreRemovesTheOldOneAndGivesTheNewOneContext) {
    AppKernel kernel;
    auto oldStore = std::make_shared<ContextProbeStore>("Shared");
    auto newStore = std::make_shared<ContextProbeStore>("Shared");

    kernel.registerAgent(oldStore);
    kernel.registerAgent(newStore);  // same name -- replaces oldStore

    EXPECT_TRUE(oldStore->removed);
    EXPECT_THROW(oldStore->probeContext(), std::logic_error);
    EXPECT_NO_THROW(newStore->probeContext());
    EXPECT_EQ(kernel.agent("Shared"), newStore);
}

}  // namespace
