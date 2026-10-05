#include "agent/command/style_commands.h"

#include <ordo/core/kernel.h>

#include "agent/style_store.h"

namespace plnr::agent {

void SetFaceStyleCommand::execute(const events::SetFaceStyleRequested& event, ordo::core::CommandContext& context) {
    auto style = context.agentAs<StyleStore>(kStyleStoreName);
    if (!style) return;  // No StyleStore registered on this kernel.
    style->setFaceStyle(event.style);
}

void SetEdgeStyleFlagCommand::execute(const events::SetEdgeStyleFlagRequested& event, ordo::core::CommandContext& context) {
    auto style = context.agentAs<StyleStore>(kStyleStoreName);
    if (!style) return;  // No StyleStore registered on this kernel.
    style->setEdgeFlag(event.flag, event.value);
}

void SetAmbientOcclusionCommand::execute(const events::SetAmbientOcclusionRequested& event, ordo::core::CommandContext& context) {
    auto style = context.agentAs<StyleStore>(kStyleStoreName);
    if (!style) return;  // No StyleStore registered on this kernel.
    style->setAmbientOcclusion(event.ambientOcclusion);
}

void SetAoStrengthCommand::execute(const events::SetAoStrengthRequested& event, ordo::core::CommandContext& context) {
    auto style = context.agentAs<StyleStore>(kStyleStoreName);
    if (!style) return;  // No StyleStore registered on this kernel.
    style->setAoStrength(event.aoStrength);
}

}  // namespace plnr::agent
