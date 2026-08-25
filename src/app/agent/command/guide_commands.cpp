#include "agent/command/guide_commands.h"

#include <ordo/core/app_kernel.h>

#include "agent/guide_store.h"

namespace plnr::agent {

void AddGuideLineCommand::execute(ordo::core::AppKernel& kernel, const events::AddGuideLineRequested& event) {
    auto guides = kernel.agentAs<GuideStore>(kGuideStoreName);
    if (!guides) return;  // No GuideStore registered on this kernel.
    guides->addGuideLine(event.point, event.dir);
}

void AddGuidePointCommand::execute(ordo::core::AppKernel& kernel, const events::AddGuidePointRequested& event) {
    auto guides = kernel.agentAs<GuideStore>(kGuideStoreName);
    if (!guides) return;  // No GuideStore registered on this kernel.
    guides->addGuidePoint(event.pos);
}

void EraseGuideCommand::execute(ordo::core::AppKernel& kernel, const events::EraseGuideRequested& event) {
    auto guides = kernel.agentAs<GuideStore>(kGuideStoreName);
    if (!guides) return;  // No GuideStore registered on this kernel.
    guides->erase(event.id);
}

void DeleteAllGuidesCommand::execute(ordo::core::AppKernel& kernel, const events::DeleteAllGuidesRequested& /*event*/) {
    auto guides = kernel.agentAs<GuideStore>(kGuideStoreName);
    if (!guides) return;  // No GuideStore registered on this kernel.
    guides->deleteAll();
}

void SetGuideHiddenCommand::execute(ordo::core::AppKernel& kernel, const events::SetGuideHiddenRequested& event) {
    auto guides = kernel.agentAs<GuideStore>(kGuideStoreName);
    if (!guides) return;  // No GuideStore registered on this kernel.
    guides->setHidden(event.id, event.hidden);
}

}  // namespace plnr::agent
