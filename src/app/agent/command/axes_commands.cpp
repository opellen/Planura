#include "agent/command/axes_commands.h"

#include <ordo/core/kernel.h>

#include "agent/axes_store.h"

namespace plnr::agent {

void SetAxesCommand::execute(const events::SetAxesRequested& event, ordo::core::CommandContext& context) {
    auto axes = context.agentAs<AxesStore>(kAxesStoreName);
    if (!axes) return;  // No AxesStore registered on this kernel.
    axes->set(event.origin, event.primaryDir, event.secondaryHint);
}

void ResetAxesCommand::execute(const events::ResetAxesRequested& /*event*/, ordo::core::CommandContext& context) {
    auto axes = context.agentAs<AxesStore>(kAxesStoreName);
    if (!axes) return;  // No AxesStore registered on this kernel.
    axes->reset();
}

}  // namespace plnr::agent
