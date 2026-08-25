#include "agent/command/style_commands.h"

#include <ordo/core/app_kernel.h>

#include "agent/style_store.h"

namespace plnr::agent {

void SetFaceStyleCommand::execute(ordo::core::AppKernel& kernel, const events::SetFaceStyleRequested& event) {
    auto style = kernel.agentAs<StyleStore>(kStyleStoreName);
    if (!style) return;  // No StyleStore registered on this kernel.
    style->setFaceStyle(event.style);
}

void SetEdgeStyleFlagCommand::execute(ordo::core::AppKernel& kernel, const events::SetEdgeStyleFlagRequested& event) {
    auto style = kernel.agentAs<StyleStore>(kStyleStoreName);
    if (!style) return;  // No StyleStore registered on this kernel.
    style->setEdgeFlag(event.flag, event.value);
}

void SetAmbientOcclusionCommand::execute(ordo::core::AppKernel& kernel,
                                          const events::SetAmbientOcclusionRequested& event) {
    auto style = kernel.agentAs<StyleStore>(kStyleStoreName);
    if (!style) return;  // No StyleStore registered on this kernel.
    style->setAmbientOcclusion(event.ambientOcclusion);
}

void SetAoStrengthCommand::execute(ordo::core::AppKernel& kernel, const events::SetAoStrengthRequested& event) {
    auto style = kernel.agentAs<StyleStore>(kStyleStoreName);
    if (!style) return;  // No StyleStore registered on this kernel.
    style->setAoStrength(event.aoStrength);
}

}  // namespace plnr::agent
