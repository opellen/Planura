#pragma once

#include <ordo/core/command.h>

#include "agent/events.h"

namespace plnr::agent {

// SetFaceStyleRequested -> StyleStore::setFaceStyle(). Not undo-wrapped (view setting); no-op check lives in the Agent.
class SetFaceStyleCommand : public ordo::core::Command<events::SetFaceStyleRequested> {
public:
    void execute(const events::SetFaceStyleRequested& event, ordo::core::CommandContext& context) override;
};

// SetEdgeStyleFlagRequested -> StyleStore::setEdgeFlag(). Not undo-wrapped (view setting).
class SetEdgeStyleFlagCommand : public ordo::core::Command<events::SetEdgeStyleFlagRequested> {
public:
    void execute(const events::SetEdgeStyleFlagRequested& event, ordo::core::CommandContext& context) override;
};

// SetAmbientOcclusionRequested -> StyleStore::setAmbientOcclusion(). Not undo-wrapped (view setting).
class SetAmbientOcclusionCommand : public ordo::core::Command<events::SetAmbientOcclusionRequested> {
public:
    void execute(const events::SetAmbientOcclusionRequested& event, ordo::core::CommandContext& context) override;
};

// SetAoStrengthRequested -> StyleStore::setAoStrength(). Not undo-wrapped (view setting).
class SetAoStrengthCommand : public ordo::core::Command<events::SetAoStrengthRequested> {
public:
    void execute(const events::SetAoStrengthRequested& event, ordo::core::CommandContext& context) override;
};

}  // namespace plnr::agent
