#pragma once

#include <ordo/core/command.h>

#include "agent/events.h"
#include "agent/transaction_manager.h"

// Undo/redo commands. UndoCaptureCommand<RealCommand, EventT> is defined in transaction_manager.h,
// included fully so main.cpp's template instantiation compiles from this header alone.
namespace plnr::agent {

// Restores the most recent UndoStore delta, applied BACKWARD. No-op when canUndo() is false.
// No TransactionManager is constructed here, so the replay can never re-record itself.
// NEVER wrapped by UndoCaptureCommand.
class UndoCommand : public ordo::core::Command<events::UndoRequested> {
public:
    void execute(ordo::core::AppKernel& kernel, const events::UndoRequested& event) override;
};

// Mirror of UndoCommand: re-applies the most recently undone delta, FORWARD. No-op when canRedo() is false.
class RedoCommand : public ordo::core::Command<events::RedoRequested> {
public:
    void execute(ordo::core::AppKernel& kernel, const events::RedoRequested& event) override;
};

}  // namespace plnr::agent
