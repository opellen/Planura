#pragma once

// Selection-AND-visibility commands: hide/unhide orchestrates both GeometryApi and
// SelectionStore (hiding deselects, industry-standard).

#include <ordo/core/command.h>

#include "agent/events.h"

namespace plnr::agent {

// Orchestrates GeometryChanged -> SelectionStore::prune(): drops selection entries whose
// backing vertex/edge/face no longer exists.
class PruneSelectionCommand : public ordo::core::Command<events::GeometryChanged> {
public:
    void execute(const events::GeometryChanged& event, ordo::core::CommandContext& context) override;
};

// Applies a click's selection semantics to SelectionStore: builds the refs vector implied by
// event.target + event.expand, degrading to the bare target if GeometryApi is absent.
class SelectCommand : public ordo::core::Command<events::SelectRequested> {
public:
    void execute(const events::SelectRequested& event, ordo::core::CommandContext& context) override;
};

// Applies a drag rectangle's region-select semantics to SelectionStore via geo::regionPickScene.
// Hidden/tag filtering applies to root entities via the query's filter param, instances via a
// post-filter (regionPickScene can't see instance-level state).
class SelectRegionCommand : public ordo::core::Command<events::SelectRegionRequested> {
public:
    void execute(const events::SelectRegionRequested& event, ordo::core::CommandContext& context) override;
};

// Selects every visible vertex/edge/face in the current editing context's Model, plus qualifying
// root-level Instance children when that context is the root. Always replace() (no SelectMode).
class SelectAllCommand : public ordo::core::Command<events::SelectAllRequested> {
public:
    void execute(const events::SelectAllRequested& event, ordo::core::CommandContext& context) override;
};

// Applies Edit > Hide: geometry->setHidden(...), and when hiding, also selection->subtract(...)
// (hide deselects).
class SetHiddenCommand : public ordo::core::Command<events::SetHiddenRequested> {
public:
    void execute(const events::SetHiddenRequested& event, ordo::core::CommandContext& context) override;
};

// Applies Edit > Unhide > All: geometry->unhideAll(). No selection side effect.
class UnhideAllCommand : public ordo::core::Command<events::UnhideAllRequested> {
public:
    void execute(const events::UnhideAllRequested& event, ordo::core::CommandContext& context) override;
};

// Applies Edit > Delete: geometry->removeEntities(selection->items()). No selection-side mutation
// of its own -- PruneSelectionCommand reacts to the GeometryChanged this fires.
class DeleteSelectionCommand : public ordo::core::Command<events::DeleteSelectionRequested> {
public:
    void execute(const events::DeleteSelectionRequested& event, ordo::core::CommandContext& context) override;
};

}  // namespace plnr::agent
