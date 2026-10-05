#include "agent/command/guide_commands.h"

#include <ordo/core/kernel.h>

#include "agent/guide_store.h"

namespace plnr::agent {

void AddGuideLineCommand::execute(const events::AddGuideLineRequested& event, ordo::core::CommandContext& context) {
    auto guides = context.agentAs<GuideStore>(kGuideStoreName);
    if (!guides) return;  // No GuideStore registered on this kernel.
    guides->addGuideLine(event.point, event.dir);
}

void AddGuidePointCommand::execute(const events::AddGuidePointRequested& event, ordo::core::CommandContext& context) {
    auto guides = context.agentAs<GuideStore>(kGuideStoreName);
    if (!guides) return;  // No GuideStore registered on this kernel.
    guides->addGuidePoint(event.pos);
}

void EraseGuideCommand::execute(const events::EraseGuideRequested& event, ordo::core::CommandContext& context) {
    auto guides = context.agentAs<GuideStore>(kGuideStoreName);
    if (!guides) return;  // No GuideStore registered on this kernel.
    guides->erase(event.id);
}

void DeleteAllGuidesCommand::execute(const events::DeleteAllGuidesRequested& /*event*/, ordo::core::CommandContext& context) {
    auto guides = context.agentAs<GuideStore>(kGuideStoreName);
    if (!guides) return;  // No GuideStore registered on this kernel.
    guides->deleteAll();
}

void SetGuideHiddenCommand::execute(const events::SetGuideHiddenRequested& event, ordo::core::CommandContext& context) {
    auto guides = context.agentAs<GuideStore>(kGuideStoreName);
    if (!guides) return;  // No GuideStore registered on this kernel.
    guides->setHidden(event.id, event.hidden);
}

}  // namespace plnr::agent
