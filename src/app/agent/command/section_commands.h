#pragma once

#include <ordo/core/command.h>

#include "agent/events.h"

namespace plnr::agent {

// AddSectionPlaneRequested -> SectionStore::addPlane(); normal normalization and auto-naming live in the Agent.
class AddSectionPlaneCommand : public ordo::core::Command<events::AddSectionPlaneRequested> {
public:
    void execute(const events::AddSectionPlaneRequested& event, ordo::core::CommandContext& context) override;
};

// RemoveSectionPlaneRequested -> SectionStore::removePlane(); unknown-id rejection lives in the Agent.
class RemoveSectionPlaneCommand : public ordo::core::Command<events::RemoveSectionPlaneRequested> {
public:
    void execute(const events::RemoveSectionPlaneRequested& event, ordo::core::CommandContext& context) override;
};

// SetSectionActiveRequested -> SectionStore::setActive(); the one-active-cut invariant lives in the Agent.
class SetSectionActiveCommand : public ordo::core::Command<events::SetSectionActiveRequested> {
public:
    void execute(const events::SetSectionActiveRequested& event, ordo::core::CommandContext& context) override;
};

// ReverseSectionRequested -> SectionStore::reverse(); unknown-id rejection lives in the Agent.
class ReverseSectionCommand : public ordo::core::Command<events::ReverseSectionRequested> {
public:
    void execute(const events::ReverseSectionRequested& event, ordo::core::CommandContext& context) override;
};

// SetSectionHiddenRequested -> SectionStore::setHidden(); unknown-id/no-op rejection lives in the Agent.
// Not wired to any Tool yet; exposed so the request/command/agent chain is complete.
class SetSectionHiddenCommand : public ordo::core::Command<events::SetSectionHiddenRequested> {
public:
    void execute(const events::SetSectionHiddenRequested& event, ordo::core::CommandContext& context) override;
};

}  // namespace plnr::agent
