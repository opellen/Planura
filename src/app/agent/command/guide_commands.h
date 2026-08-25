#pragma once

#include <ordo/core/command.h>

#include "agent/events.h"

namespace plnr::agent {

// Orchestrates AddGuideLineRequested -> GuideStore::addGuideLine(...). Thin,
// per the Ordo role policy: dir normalization and id assignment live in the
// Agent.
class AddGuideLineCommand : public ordo::core::Command<events::AddGuideLineRequested> {
public:
    void execute(ordo::core::AppKernel& kernel, const events::AddGuideLineRequested& event) override;
};

// Orchestrates AddGuidePointRequested -> GuideStore::addGuidePoint(...).
// Thin, per the Ordo role policy: id assignment lives in the Agent.
class AddGuidePointCommand : public ordo::core::Command<events::AddGuidePointRequested> {
public:
    void execute(ordo::core::AppKernel& kernel, const events::AddGuidePointRequested& event) override;
};

// Orchestrates EraseGuideRequested -> GuideStore::erase(...). Thin, per the
// Ordo role policy: unknown-id rejection lives in the Agent.
class EraseGuideCommand : public ordo::core::Command<events::EraseGuideRequested> {
public:
    void execute(ordo::core::AppKernel& kernel, const events::EraseGuideRequested& event) override;
};

// Orchestrates DeleteAllGuidesRequested -> GuideStore::deleteAll()
// (the reference modeler Edit > Delete Guides).
class DeleteAllGuidesCommand : public ordo::core::Command<events::DeleteAllGuidesRequested> {
public:
    void execute(ordo::core::AppKernel& kernel, const events::DeleteAllGuidesRequested& event) override;
};

// Orchestrates SetGuideHiddenRequested -> GuideStore::setHidden(...). Thin,
// per the Ordo role policy: unknown-id/no-op rejection lives in the Agent.
class SetGuideHiddenCommand : public ordo::core::Command<events::SetGuideHiddenRequested> {
public:
    void execute(ordo::core::AppKernel& kernel, const events::SetGuideHiddenRequested& event) override;
};

}  // namespace plnr::agent
