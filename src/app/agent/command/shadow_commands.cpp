#include "agent/command/shadow_commands.h"

#include <ordo/core/kernel.h>

#include "agent/shadow_store.h"

namespace plnr::agent {

void SetUseSunForShadingCommand::execute(const events::SetUseSunForShadingRequested& event, ordo::core::CommandContext& context) {
    auto shadow = context.agentAs<ShadowStore>(kShadowStoreName);
    if (!shadow) return;  // No ShadowStore registered on this kernel.
    shadow->setUseSunForShading(event.useSunForShading);
}

void SetShowShadowsCommand::execute(const events::SetShowShadowsRequested& event, ordo::core::CommandContext& context) {
    auto shadow = context.agentAs<ShadowStore>(kShadowStoreName);
    if (!shadow) return;  // No ShadowStore registered on this kernel.
    shadow->setShowShadows(event.showShadows);
}

void SetSunPositionCommand::execute(const events::SetSunPositionRequested& event, ordo::core::CommandContext& context) {
    auto shadow = context.agentAs<ShadowStore>(kShadowStoreName);
    if (!shadow) return;  // No ShadowStore registered on this kernel.
    shadow->setPosition(event.latitudeDeg, event.longitudeDeg);
}

void SetSunDateTimeCommand::execute(const events::SetSunDateTimeRequested& event, ordo::core::CommandContext& context) {
    auto shadow = context.agentAs<ShadowStore>(kShadowStoreName);
    if (!shadow) return;  // No ShadowStore registered on this kernel.
    shadow->setDateTime(event.month, event.day, event.hourLocal);
}

void SetShadowLightCommand::execute(const events::SetShadowLightRequested& event, ordo::core::CommandContext& context) {
    auto shadow = context.agentAs<ShadowStore>(kShadowStoreName);
    if (!shadow) return;  // No ShadowStore registered on this kernel.
    shadow->setLight(event.light);
}

void SetShadowDarkCommand::execute(const events::SetShadowDarkRequested& event, ordo::core::CommandContext& context) {
    auto shadow = context.agentAs<ShadowStore>(kShadowStoreName);
    if (!shadow) return;  // No ShadowStore registered on this kernel.
    shadow->setDark(event.dark);
}

}  // namespace plnr::agent
