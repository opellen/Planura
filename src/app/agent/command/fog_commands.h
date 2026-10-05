#pragma once

#include <ordo/core/command.h>

#include "agent/events.h"

namespace plnr::agent {

// SetFogEnabledRequested -> FogStore::setEnabled(). Not undo-wrapped: fog is a view setting.
class SetFogEnabledCommand : public ordo::core::Command<events::SetFogEnabledRequested> {
public:
    void execute(const events::SetFogEnabledRequested& event, ordo::core::CommandContext& context) override;
};

// SetFogRangeRequested -> FogStore::setRange(). Not undo-wrapped (view setting).
class SetFogRangeCommand : public ordo::core::Command<events::SetFogRangeRequested> {
public:
    void execute(const events::SetFogRangeRequested& event, ordo::core::CommandContext& context) override;
};

// SetFogUseBackgroundColorRequested -> FogStore::setUseBackgroundColor(). Not undo-wrapped (view setting).
class SetFogUseBackgroundColorCommand : public ordo::core::Command<events::SetFogUseBackgroundColorRequested> {
public:
    void execute(const events::SetFogUseBackgroundColorRequested& event, ordo::core::CommandContext& context) override;
};

}  // namespace plnr::agent
