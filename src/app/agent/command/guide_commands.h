#pragma once

#include <ordo/core/command.h>

#include "agent/events.h"

namespace plnr::agent {

// Orchestrates AddGuideLineRequested -> GuideStore::addGuideLine(...). Thin,
// per the Ordo role policy: dir normalization and id assignment live in the
// Agent.
class AddGuideLineCommand : public ordo::core::Command<events::AddGuideLineRequested> {
public:
    void execute(const events::AddGuideLineRequested& event, ordo::core::CommandContext& context) override;
};

// Orchestrates AddGuidePointRequested -> GuideStore::addGuidePoint(...).
// Thin, per the Ordo role policy: id assignment lives in the Agent.
class AddGuidePointCommand : public ordo::core::Command<events::AddGuidePointRequested> {
public:
    void execute(const events::AddGuidePointRequested& event, ordo::core::CommandContext& context) override;
};

// Orchestrates EraseGuideRequested -> GuideStore::erase(...). Thin, per the
// Ordo role policy: unknown-id rejection lives in the Agent.
class EraseGuideCommand : public ordo::core::Command<events::EraseGuideRequested> {
public:
    void execute(const events::EraseGuideRequested& event, ordo::core::CommandContext& context) override;
};

// Orchestrates DeleteAllGuidesRequested -> GuideStore::deleteAll()
// (the reference modeler Edit > Delete Guides).
class DeleteAllGuidesCommand : public ordo::core::Command<events::DeleteAllGuidesRequested> {
public:
    void execute(const events::DeleteAllGuidesRequested& event, ordo::core::CommandContext& context) override;
};

// Orchestrates SetGuideHiddenRequested -> GuideStore::setHidden(...). Thin,
// per the Ordo role policy: unknown-id/no-op rejection lives in the Agent.
class SetGuideHiddenCommand : public ordo::core::Command<events::SetGuideHiddenRequested> {
public:
    void execute(const events::SetGuideHiddenRequested& event, ordo::core::CommandContext& context) override;
};

}  // namespace plnr::agent
