#include "agent/command/section_commands.h"

#include <ordo/core/kernel.h>

#include "agent/section_store.h"

namespace plnr::agent {

void AddSectionPlaneCommand::execute(const events::AddSectionPlaneRequested& event, ordo::core::CommandContext& context) {
    auto sections = context.agentAs<SectionStore>(kSectionStoreName);
    if (!sections) return;  // No SectionStore registered on this kernel.
    sections->addPlane(event.point, event.normal, event.name);
}

void RemoveSectionPlaneCommand::execute(const events::RemoveSectionPlaneRequested& event, ordo::core::CommandContext& context) {
    auto sections = context.agentAs<SectionStore>(kSectionStoreName);
    if (!sections) return;  // No SectionStore registered on this kernel.
    sections->removePlane(event.id);
}

void SetSectionActiveCommand::execute(const events::SetSectionActiveRequested& event, ordo::core::CommandContext& context) {
    auto sections = context.agentAs<SectionStore>(kSectionStoreName);
    if (!sections) return;  // No SectionStore registered on this kernel.
    sections->setActive(event.id, event.active);
}

void ReverseSectionCommand::execute(const events::ReverseSectionRequested& event, ordo::core::CommandContext& context) {
    auto sections = context.agentAs<SectionStore>(kSectionStoreName);
    if (!sections) return;  // No SectionStore registered on this kernel.
    sections->reverse(event.id);
}

void SetSectionHiddenCommand::execute(const events::SetSectionHiddenRequested& event, ordo::core::CommandContext& context) {
    auto sections = context.agentAs<SectionStore>(kSectionStoreName);
    if (!sections) return;  // No SectionStore registered on this kernel.
    sections->setHidden(event.id, event.hidden);
}

}  // namespace plnr::agent
