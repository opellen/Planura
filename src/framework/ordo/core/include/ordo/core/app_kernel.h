#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <typeinfo>
#include <unordered_map>

#include <ordo/core/command.h>
#include <ordo/core/dispatcher.h>
#include <ordo/core/agent.h>
#include <ordo/core/agent_context.h>

namespace ordo::core {

// Facade over one Dispatcher plus named Agents and per-event Command
// factories. Instantiable, never a singleton -- two instances are fully isolated.
class AppKernel {
public:
    AppKernel();

    Dispatcher& dispatcher();

    // Registers (or replaces, by name) an agent: gives it this kernel's
    // AgentContext and calls onRegister(). A replaced agent gets onRemove() first.
    void registerAgent(std::shared_ptr<Agent> agent);

    // Returns nullptr if no agent is registered under that name.
    std::shared_ptr<Agent> agent(std::string_view name);

    // dynamic_pointer_cast convenience over agent(); nullptr if absent, or if the
    // registered agent is not actually an AgentT.
    template <typename AgentT>
    std::shared_ptr<AgentT> agentAs(std::string_view name) {
        return std::dynamic_pointer_cast<AgentT>(agent(name));
    }

    // Calls onRemove() on the agent and drops it. Returns false (no-op) if no
    // agent is registered under that name.
    bool removeAgent(std::string_view name);

    // Subscribes a factory that constructs a fresh CommandT and calls
    // execute(*this, event) per dispatched EventT. Re-registering for an
    // already-mapped EventT replaces the old factory.
    template <typename EventT, typename CommandT>
    void registerCommand() {
        static_assert(std::is_base_of<Command<EventT>, CommandT>::value,
                      "CommandT must derive from Command<EventT>");
        removeCommand<EventT>();
        AppKernel* kernel = this;
        dispatcher_.subscribe<EventT>(commandOwner<EventT>(), [kernel](const EventT& event) {
            auto command = std::make_unique<CommandT>();
            command->execute(*kernel, event);
        });
    }

    // Unsubscribes the command factory (if any) registered for EventT. Other
    // event types' command factories on this kernel are unaffected.
    template <typename EventT>
    void removeCommand() {
        dispatcher_.unsubscribe(commandOwner<EventT>());
    }

    // Convenience for dispatcher().dispatch(e).
    template <typename EventT>
    void send(const EventT& event) {
        dispatcher_.dispatch(event);
    }

private:
    // Per-(kernel, EventT) stable-address token used as the Dispatcher owner
    // cookie, so removeCommand<EventT>() tears down only its own factory
    // (a plain `this` would remove ALL of this kernel's subscriptions).
    template <typename EventT>
    const void* commandOwner() {
        return &commandOwnerTokens_[typeid(EventT).hash_code()];
    }

    Dispatcher dispatcher_;
    AgentContext storeContext_;  // constructed after dispatcher_ -- member order matters
    std::unordered_map<std::string, std::shared_ptr<Agent>> stores_;
    std::unordered_map<std::size_t, int> commandOwnerTokens_;
};

}  // namespace ordo::core
