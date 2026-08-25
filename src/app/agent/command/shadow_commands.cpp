#include "agent/command/shadow_commands.h"

#include <ordo/core/app_kernel.h>

#include "agent/shadow_store.h"

namespace plnr::agent {

void SetUseSunForShadingCommand::execute(ordo::core::AppKernel& kernel,
                                          const events::SetUseSunForShadingRequested& event) {
    auto shadow = kernel.agentAs<ShadowStore>(kShadowStoreName);
    if (!shadow) return;  // No ShadowStore registered on this kernel.
    shadow->setUseSunForShading(event.useSunForShading);
}

void SetShowShadowsCommand::execute(ordo::core::AppKernel& kernel, const events::SetShowShadowsRequested& event) {
    auto shadow = kernel.agentAs<ShadowStore>(kShadowStoreName);
    if (!shadow) return;  // No ShadowStore registered on this kernel.
    shadow->setShowShadows(event.showShadows);
}

void SetSunPositionCommand::execute(ordo::core::AppKernel& kernel, const events::SetSunPositionRequested& event) {
    auto shadow = kernel.agentAs<ShadowStore>(kShadowStoreName);
    if (!shadow) return;  // No ShadowStore registered on this kernel.
    shadow->setPosition(event.latitudeDeg, event.longitudeDeg);
}

void SetSunDateTimeCommand::execute(ordo::core::AppKernel& kernel, const events::SetSunDateTimeRequested& event) {
    auto shadow = kernel.agentAs<ShadowStore>(kShadowStoreName);
    if (!shadow) return;  // No ShadowStore registered on this kernel.
    shadow->setDateTime(event.month, event.day, event.hourLocal);
}

void SetShadowLightCommand::execute(ordo::core::AppKernel& kernel, const events::SetShadowLightRequested& event) {
    auto shadow = kernel.agentAs<ShadowStore>(kShadowStoreName);
    if (!shadow) return;  // No ShadowStore registered on this kernel.
    shadow->setLight(event.light);
}

void SetShadowDarkCommand::execute(ordo::core::AppKernel& kernel, const events::SetShadowDarkRequested& event) {
    auto shadow = kernel.agentAs<ShadowStore>(kShadowStoreName);
    if (!shadow) return;  // No ShadowStore registered on this kernel.
    shadow->setDark(event.dark);
}

}  // namespace plnr::agent
