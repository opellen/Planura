#include "agent/command/fog_commands.h"

#include <ordo/core/app_kernel.h>

#include "agent/fog_store.h"

namespace plnr::agent {

void SetFogEnabledCommand::execute(ordo::core::AppKernel& kernel, const events::SetFogEnabledRequested& event) {
    auto fog = kernel.agentAs<FogStore>(kFogStoreName);
    if (!fog) return;  // No FogStore registered on this kernel.
    fog->setEnabled(event.enabled);
}

void SetFogRangeCommand::execute(ordo::core::AppKernel& kernel, const events::SetFogRangeRequested& event) {
    auto fog = kernel.agentAs<FogStore>(kFogStoreName);
    if (!fog) return;  // No FogStore registered on this kernel.
    fog->setRange(event.startDistance, event.endDistance);
}

void SetFogUseBackgroundColorCommand::execute(ordo::core::AppKernel& kernel,
                                               const events::SetFogUseBackgroundColorRequested& event) {
    auto fog = kernel.agentAs<FogStore>(kFogStoreName);
    if (!fog) return;  // No FogStore registered on this kernel.
    fog->setUseBackgroundColor(event.useBackgroundColor);
}

}  // namespace plnr::agent
