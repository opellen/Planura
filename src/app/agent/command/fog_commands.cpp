#include "agent/command/fog_commands.h"

#include <ordo/core/kernel.h>

#include "agent/fog_store.h"

namespace plnr::agent {

void SetFogEnabledCommand::execute(const events::SetFogEnabledRequested& event, ordo::core::CommandContext& context) {
    auto fog = context.agentAs<FogStore>(kFogStoreName);
    if (!fog) return;  // No FogStore registered on this kernel.
    fog->setEnabled(event.enabled);
}

void SetFogRangeCommand::execute(const events::SetFogRangeRequested& event, ordo::core::CommandContext& context) {
    auto fog = context.agentAs<FogStore>(kFogStoreName);
    if (!fog) return;  // No FogStore registered on this kernel.
    fog->setRange(event.startDistance, event.endDistance);
}

void SetFogUseBackgroundColorCommand::execute(const events::SetFogUseBackgroundColorRequested& event, ordo::core::CommandContext& context) {
    auto fog = context.agentAs<FogStore>(kFogStoreName);
    if (!fog) return;  // No FogStore registered on this kernel.
    fog->setUseBackgroundColor(event.useBackgroundColor);
}

}  // namespace plnr::agent
