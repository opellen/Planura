#pragma once

// Enter/exit editing-context commands (double-click-to-edit-in-place).

#include <ordo/core/command.h>

#include "agent/events.h"

namespace plnr::agent {

// Validates event.instanceId is a direct child of the current editing context, then
// editContext->push(instanceId) + selection->clear(). Unknown/foreign instanceId is a no-op.
class EnterContextCommand : public ordo::core::Command<events::EnterContextRequested> {
public:
    void execute(const events::EnterContextRequested& event, ordo::core::CommandContext& context) override;
};

// editContext->pop() + selection->clear() (exiting a context deselects too).
// A no-op at the root context (EditContextStore::pop() already handles that).
class ExitContextCommand : public ordo::core::Command<events::ExitContextRequested> {
public:
    void execute(const events::ExitContextRequested& event, ordo::core::CommandContext& context) override;
};

}  // namespace plnr::agent
