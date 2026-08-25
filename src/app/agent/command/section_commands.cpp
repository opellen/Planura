#include "agent/command/section_commands.h"

#include <ordo/core/app_kernel.h>

#include "agent/section_store.h"

namespace plnr::agent {

void AddSectionPlaneCommand::execute(ordo::core::AppKernel& kernel, const events::AddSectionPlaneRequested& event) {
    auto sections = kernel.agentAs<SectionStore>(kSectionStoreName);
    if (!sections) return;  // No SectionStore registered on this kernel.
    sections->addPlane(event.point, event.normal, event.name);
}

void RemoveSectionPlaneCommand::execute(ordo::core::AppKernel& kernel, const events::RemoveSectionPlaneRequested& event) {
    auto sections = kernel.agentAs<SectionStore>(kSectionStoreName);
    if (!sections) return;  // No SectionStore registered on this kernel.
    sections->removePlane(event.id);
}

void SetSectionActiveCommand::execute(ordo::core::AppKernel& kernel, const events::SetSectionActiveRequested& event) {
    auto sections = kernel.agentAs<SectionStore>(kSectionStoreName);
    if (!sections) return;  // No SectionStore registered on this kernel.
    sections->setActive(event.id, event.active);
}

void ReverseSectionCommand::execute(ordo::core::AppKernel& kernel, const events::ReverseSectionRequested& event) {
    auto sections = kernel.agentAs<SectionStore>(kSectionStoreName);
    if (!sections) return;  // No SectionStore registered on this kernel.
    sections->reverse(event.id);
}

void SetSectionHiddenCommand::execute(ordo::core::AppKernel& kernel, const events::SetSectionHiddenRequested& event) {
    auto sections = kernel.agentAs<SectionStore>(kSectionStoreName);
    if (!sections) return;  // No SectionStore registered on this kernel.
    sections->setHidden(event.id, event.hidden);
}

}  // namespace plnr::agent
