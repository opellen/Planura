#pragma once

#include <ordo/core/command.h>

#include "agent/events.h"

namespace plnr::agent {

// Orchestrates AddDimensionRequested -> AnnotationStore::addDimension(...).
// Thin, per the Ordo role policy: looks up GeometryApi's model (dimensions
// need it to seed lastA/lastB -- see AnnotationStore::addDimension's own
// comment) and forwards; the association/no-op logic itself lives in the
// Agent.
class AddDimensionCommand : public ordo::core::Command<events::AddDimensionRequested> {
public:
    void execute(ordo::core::AppKernel& kernel, const events::AddDimensionRequested& event) override;
};

// Orchestrates AddScreenTextRequested -> AnnotationStore::addScreenText(...).
class AddScreenTextCommand : public ordo::core::Command<events::AddScreenTextRequested> {
public:
    void execute(ordo::core::AppKernel& kernel, const events::AddScreenTextRequested& event) override;
};

// Orchestrates AddLeaderTextRequested -> AnnotationStore::addLeaderText(...).
class AddLeaderTextCommand : public ordo::core::Command<events::AddLeaderTextRequested> {
public:
    void execute(ordo::core::AppKernel& kernel, const events::AddLeaderTextRequested& event) override;
};

// Orchestrates SetAnnotationTextRequested -> AnnotationStore::setText(...).
// Thin, per the Ordo role policy: dimension-vs-text-note dispatch and the
// no-op/unknown-id rejection both live in the Agent.
class SetAnnotationTextCommand : public ordo::core::Command<events::SetAnnotationTextRequested> {
public:
    void execute(ordo::core::AppKernel& kernel, const events::SetAnnotationTextRequested& event) override;
};

// Orchestrates RemoveAnnotationRequested -> AnnotationStore::remove(...).
class RemoveAnnotationCommand : public ordo::core::Command<events::RemoveAnnotationRequested> {
public:
    void execute(ordo::core::AppKernel& kernel, const events::RemoveAnnotationRequested& event) override;
};

// Orchestrates RemoveAllAnnotationsRequested -> AnnotationStore::removeAll().
class RemoveAllAnnotationsCommand : public ordo::core::Command<events::RemoveAllAnnotationsRequested> {
public:
    void execute(ordo::core::AppKernel& kernel, const events::RemoveAllAnnotationsRequested& event) override;
};

// Reacts to GeometryChanged: prune (replays PruneSelectionCommand),
// AnnotationStore::refreshAssociations, DocumentStore::markDirty(),
// UndoStore::notifyMutation() -- main.cpp registers THIS class for
// GeometryChanged instead of PruneSelectionCommand (Ordo replaces, never
// stacks, a Command registration); markDirty/notifyMutation run before
// the null-check below.
class GeometryChangedCommand : public ordo::core::Command<events::GeometryChanged> {
public:
    void execute(ordo::core::AppKernel& kernel, const events::GeometryChanged& event) override;
};

}  // namespace plnr::agent
