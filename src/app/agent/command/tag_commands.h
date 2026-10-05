#pragma once

#include <ordo/core/command.h>

#include "agent/events.h"

namespace plnr::agent {

// TagCreateRequested -> TagStore::createTag(); auto-naming and id assignment live in the Agent.
class TagCreateCommand : public ordo::core::Command<events::TagCreateRequested> {
public:
    void execute(const events::TagCreateRequested& event, ordo::core::CommandContext& context) override;
};

// TagAssignRequested -> TagStore::assignTag(); unknown-tagId and per-ref no-op checks live in the Agent.
class TagAssignCommand : public ordo::core::Command<events::TagAssignRequested> {
public:
    void execute(const events::TagAssignRequested& event, ordo::core::CommandContext& context) override;
};

// TagStore::setTagVisible(); hiding a tag also prunes SelectionStore of entities that just became invisible.
class TagVisibilityCommand : public ordo::core::Command<events::TagVisibilityRequested> {
public:
    void execute(const events::TagVisibilityRequested& event, ordo::core::CommandContext& context) override;
};

}  // namespace plnr::agent
