#pragma once

// Solid Tools command (Union/Outer Shell/Subtract/Trim/Intersect/Split), driven by
// SolidOpRequested. Orchestrates GeometryApi + MaterialRepository + SelectionStore.

#include <ordo/core/command.h>

#include "agent/events.h"

namespace plnr::agent {

// Hands event.op/instanceIds to GeometryApi::applySolidOp. On rejection, reports event.hint
// via StatusHintChanged. On success, best-effort carries each result face's FRONT material from
// its source (Agents can't reach across each other), then clears the selection.
class SolidOpCommand : public ordo::core::Command<events::SolidOpRequested> {
public:
    void execute(ordo::core::AppKernel& kernel, const events::SolidOpRequested& event) override;
};

}  // namespace plnr::agent
