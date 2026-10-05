#pragma once

#include <ordo/core/command.h>

#include "agent/events.h"

namespace plnr::agent {

// Orchestrates SetAxesRequested -> AxesStore::set(...). Thin, per the Ordo
// role policy: re-orthonormalization/degenerate-input rejection lives in the
// Agent.
class SetAxesCommand : public ordo::core::Command<events::SetAxesRequested> {
public:
    void execute(const events::SetAxesRequested& event, ordo::core::CommandContext& context) override;
};

// Orchestrates ResetAxesRequested -> AxesStore::reset(). Thin, per the Ordo
// role policy: already-at-default no-op rejection lives in the Agent.
class ResetAxesCommand : public ordo::core::Command<events::ResetAxesRequested> {
public:
    void execute(const events::ResetAxesRequested& event, ordo::core::CommandContext& context) override;
};

}  // namespace plnr::agent
