#include "agent/command/axes_commands.h"

#include <ordo/core/app_kernel.h>

#include "agent/axes_store.h"

namespace plnr::agent {

void SetAxesCommand::execute(ordo::core::AppKernel& kernel, const events::SetAxesRequested& event) {
    auto axes = kernel.agentAs<AxesStore>(kAxesStoreName);
    if (!axes) return;  // No AxesStore registered on this kernel.
    axes->set(event.origin, event.primaryDir, event.secondaryHint);
}

void ResetAxesCommand::execute(ordo::core::AppKernel& kernel, const events::ResetAxesRequested& /*event*/) {
    auto axes = kernel.agentAs<AxesStore>(kAxesStoreName);
    if (!axes) return;  // No AxesStore registered on this kernel.
    axes->reset();
}

}  // namespace plnr::agent
