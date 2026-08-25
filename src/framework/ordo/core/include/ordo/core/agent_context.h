#pragma once

#include <memory>
#include <string_view>

#include <ordo/core/dispatcher.h>

namespace ordo::core {

class AppKernel;
class Agent;

// Passkey minted only by AppKernel: methods taking a KernelKey are publicly
// visible but callable only from AppKernel code. Confines the friend surface
// to this empty token instead of opening a whole class to the kernel.
class KernelKey {
    friend class AppKernel;
    KernelKey() = default;

public:
    KernelKey(const KernelKey&) = delete;
    KernelKey& operator=(const KernelKey&) = delete;
};

// Narrow capability view handed to Agents at registration (least privilege):
// an Agent may publish events and look up sibling agents -- nothing else.
// Owned by the kernel; lifetime == kernel lifetime, so a raw pointer to it
// held by a registered Agent can never dangle.
class AgentContext {
public:
    // Constructible only by AppKernel (KernelKey passkey).
    AgentContext(KernelKey, AppKernel& kernel, Dispatcher& dispatcher);

    template <typename EventT>
    void send(const EventT& event) { dispatcher_->dispatch(event); }

    // Returns nullptr if no agent is registered under that name.
    std::shared_ptr<Agent> agent(std::string_view name) const;

    // dynamic_pointer_cast convenience over agent(); nullptr if absent, or if
    // the registered agent is not actually an AgentT.
    template <typename AgentT>
    std::shared_ptr<AgentT> agentAs(std::string_view name) const {
        return std::dynamic_pointer_cast<AgentT>(agent(name));
    }

private:
    AppKernel* kernel_;
    Dispatcher* dispatcher_;
};

}  // namespace ordo::core
