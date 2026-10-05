#pragma once

#include <ordo/core/command.h>

#include "agent/events.h"

namespace plnr::agent {

// AddDimensionRequested -> AnnotationStore::addDimension(). Only fetches GeometryApi's model (seeds lastA/lastB);
// association logic lives in the Agent.
class AddDimensionCommand : public ordo::core::Command<events::AddDimensionRequested> {
public:
    void execute(const events::AddDimensionRequested& event, ordo::core::CommandContext& context) override;
};

// Orchestrates AddScreenTextRequested -> AnnotationStore::addScreenText(...).
class AddScreenTextCommand : public ordo::core::Command<events::AddScreenTextRequested> {
public:
    void execute(const events::AddScreenTextRequested& event, ordo::core::CommandContext& context) override;
};

// Orchestrates AddLeaderTextRequested -> AnnotationStore::addLeaderText(...).
class AddLeaderTextCommand : public ordo::core::Command<events::AddLeaderTextRequested> {
public:
    void execute(const events::AddLeaderTextRequested& event, ordo::core::CommandContext& context) override;
};

// SetAnnotationTextRequested -> AnnotationStore::setText(); dimension/text dispatch and no-op rejection live in the Agent.
class SetAnnotationTextCommand : public ordo::core::Command<events::SetAnnotationTextRequested> {
public:
    void execute(const events::SetAnnotationTextRequested& event, ordo::core::CommandContext& context) override;
};

// Orchestrates RemoveAnnotationRequested -> AnnotationStore::remove(...).
class RemoveAnnotationCommand : public ordo::core::Command<events::RemoveAnnotationRequested> {
public:
    void execute(const events::RemoveAnnotationRequested& event, ordo::core::CommandContext& context) override;
};

// Orchestrates RemoveAllAnnotationsRequested -> AnnotationStore::removeAll().
class RemoveAllAnnotationsCommand : public ordo::core::Command<events::RemoveAllAnnotationsRequested> {
public:
    void execute(const events::RemoveAllAnnotationsRequested& event, ordo::core::CommandContext& context) override;
};

// GeometryChanged handler: replays PruneSelectionCommand, refreshAssociations, markDirty(), notifyMutation().
// main.cpp registers THIS instead of PruneSelectionCommand (Ordo replaces, never stacks); markDirty/notifyMutation run before the null-check.
class GeometryChangedCommand : public ordo::core::Command<events::GeometryChanged> {
public:
    void execute(const events::GeometryChanged& event, ordo::core::CommandContext& context) override;
};

}  // namespace plnr::agent
