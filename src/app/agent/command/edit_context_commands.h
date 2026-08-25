#pragma once

// Enter/exit editing-context commands (double-click-to-edit-in-place).

#include <ordo/core/command.h>

#include "agent/events.h"

namespace plnr::agent {

// Validates event.instanceId is a direct child of the current editing context, then
// editContext->push(instanceId) + selection->clear(). Unknown/foreign instanceId is a no-op.
class EnterContextCommand : public ordo::core::Command<events::EnterContextRequested> {
public:
    void execute(ordo::core::AppKernel& kernel, const events::EnterContextRequested& event) override;
};

// editContext->pop() + selection->clear() (exiting a context deselects too).
// A no-op at the root context (EditContextStore::pop() already handles that).
class ExitContextCommand : public ordo::core::Command<events::ExitContextRequested> {
public:
    void execute(ordo::core::AppKernel& kernel, const events::ExitContextRequested& event) override;
};

}  // namespace plnr::agent
