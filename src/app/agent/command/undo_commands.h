#pragma once

#include <ordo/core/command.h>

#include "agent/events.h"
#include "agent/transaction.h"

// Undo/redo commands. UndoCaptureCommand<RealCommand, EventT> is defined in transaction.h,
// included fully so main.cpp's template instantiation compiles from this header alone.
namespace plnr::agent {

// Restores the most recent UndoStore delta, applied BACKWARD. No-op when canUndo() is false.
// No Transaction is constructed here, so the replay can never re-record itself.
// NEVER wrapped by UndoCaptureCommand.
class UndoCommand : public ordo::core::Command<events::UndoRequested> {
public:
    void execute(const events::UndoRequested& event, ordo::core::CommandContext& context) override;
};

// Mirror of UndoCommand: re-applies the most recently undone delta, FORWARD. No-op when canRedo() is false.
class RedoCommand : public ordo::core::Command<events::RedoRequested> {
public:
    void execute(const events::RedoRequested& event, ordo::core::CommandContext& context) override;
};

}  // namespace plnr::agent
