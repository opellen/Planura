#pragma once

#include <ordo/core/command.h>

#include "agent/events.h"

namespace plnr::agent {

// Orchestrates SetAxesRequested -> AxesStore::set(...). Thin, per the Ordo
// role policy: re-orthonormalization/degenerate-input rejection lives in the
// Agent.
class SetAxesCommand : public ordo::core::Command<events::SetAxesRequested> {
public:
    void execute(ordo::core::AppKernel& kernel, const events::SetAxesRequested& event) override;
};

// Orchestrates ResetAxesRequested -> AxesStore::reset(). Thin, per the Ordo
// role policy: already-at-default no-op rejection lives in the Agent.
class ResetAxesCommand : public ordo::core::Command<events::ResetAxesRequested> {
public:
    void execute(ordo::core::AppKernel& kernel, const events::ResetAxesRequested& event) override;
};

}  // namespace plnr::agent
