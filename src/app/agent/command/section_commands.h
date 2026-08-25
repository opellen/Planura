#pragma once

#include <ordo/core/command.h>

#include "agent/events.h"

namespace plnr::agent {

// Orchestrates AddSectionPlaneRequested -> SectionStore::addPlane(...).
// Thin, per the Ordo role policy: normal normalization/auto-naming live in
// the Agent.
class AddSectionPlaneCommand : public ordo::core::Command<events::AddSectionPlaneRequested> {
public:
    void execute(ordo::core::AppKernel& kernel, const events::AddSectionPlaneRequested& event) override;
};

// Orchestrates RemoveSectionPlaneRequested -> SectionStore::removePlane(...).
// Thin, per the Ordo role policy: unknown-id rejection lives in the Agent.
class RemoveSectionPlaneCommand : public ordo::core::Command<events::RemoveSectionPlaneRequested> {
public:
    void execute(ordo::core::AppKernel& kernel, const events::RemoveSectionPlaneRequested& event) override;
};

// Orchestrates SetSectionActiveRequested -> SectionStore::setActive(...).
// Thin, per the Ordo role policy: the one-active-cut invariant lives in the
// Agent.
class SetSectionActiveCommand : public ordo::core::Command<events::SetSectionActiveRequested> {
public:
    void execute(ordo::core::AppKernel& kernel, const events::SetSectionActiveRequested& event) override;
};

// Orchestrates ReverseSectionRequested -> SectionStore::reverse(...). Thin,
// per the Ordo role policy: unknown-id rejection lives in the Agent.
class ReverseSectionCommand : public ordo::core::Command<events::ReverseSectionRequested> {
public:
    void execute(ordo::core::AppKernel& kernel, const events::ReverseSectionRequested& event) override;
};

// Orchestrates SetSectionHiddenRequested -> SectionStore::setHidden(...).
// Thin, per the Ordo role policy: unknown-id/no-op rejection lives in the
// Agent. Not wired to any Tool yet -- exists today so the request/
// command/agent chain is already complete, same "expose agent ops now"
// precedent as ResetAxesRequested/EraseGuideRequested.
class SetSectionHiddenCommand : public ordo::core::Command<events::SetSectionHiddenRequested> {
public:
    void execute(ordo::core::AppKernel& kernel, const events::SetSectionHiddenRequested& event) override;
};

}  // namespace plnr::agent
