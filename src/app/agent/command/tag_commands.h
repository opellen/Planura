#pragma once

#include <ordo/core/command.h>

#include "agent/events.h"

namespace plnr::agent {

// Orchestrates TagCreateRequested -> TagStore::createTag(). Thin, per the
// Ordo role policy: auto-naming and id assignment live in the Agent.
class TagCreateCommand : public ordo::core::Command<events::TagCreateRequested> {
public:
    void execute(ordo::core::AppKernel& kernel, const events::TagCreateRequested& event) override;
};

// Orchestrates TagAssignRequested -> TagStore::assignTag(). Thin, per the
// Ordo role policy: unknown-tagId rejection and the per-ref no-op check
// live in the Agent.
class TagAssignCommand : public ordo::core::Command<events::TagAssignRequested> {
public:
    void execute(ordo::core::AppKernel& kernel, const events::TagAssignRequested& event) override;
};

// Applies the reference modeler's tag-visibility semantics: TagStore::setTagVisible(...),
// and when hiding a tag, also SelectionStore::prune(...) to drop any
// currently-selected entity whose tag just became invisible. Two coarse
// agent calls -- still thin orchestration, mirroring SetHiddenCommand's
// GeometryApi/SelectionStore pairing in selection_commands.h.
class TagVisibilityCommand : public ordo::core::Command<events::TagVisibilityRequested> {
public:
    void execute(ordo::core::AppKernel& kernel, const events::TagVisibilityRequested& event) override;
};

}  // namespace plnr::agent
