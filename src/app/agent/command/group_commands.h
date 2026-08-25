#pragma once

// Make Group / Make Component / Explode commands: orchestrate SelectionStore + GeometryApi.

#include <ordo/core/command.h>

#include "agent/events.h"

namespace plnr::agent {

// Applies Make Group / Make Component: GeometryApi::makeGroup(), then clears the selection on
// success (the moved entities' old refs are dead anyway).
class GroupCreateCommand : public ordo::core::Command<events::GroupCreateRequested> {
public:
    void execute(ordo::core::AppKernel& kernel, const events::GroupCreateRequested& event) override;
};

// Applies Explode. event.instanceId == kInvalidId explodes every selected Instance (subtracting
// those refs from the selection); a nonzero instanceId explodes just that one.
// An unknown id is a no-op resolved inside the Agent.
class ExplodeCommand : public ordo::core::Command<events::ExplodeRequested> {
public:
    void execute(ordo::core::AppKernel& kernel, const events::ExplodeRequested& event) override;
};

}  // namespace plnr::agent
