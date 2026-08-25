#pragma once

#include <cstddef>
#include <optional>
#include <string_view>
#include <vector>

#include <ordo/core/agent.h>

#include "agent/events.h"
#include "agent/transaction_delta.h"

namespace plnr::agent {

inline constexpr std::string_view kUndoStoreName = "undo";

class TransactionManager;  // forward declared; only a pointer to one is stored here.

// Owns the undo/redo stacks of TransactionDelta. Capture itself (journals, aux-agent snapshots)
// is TransactionManager's job; UndoStore just holds the stacks and relays the mutation ping.
class UndoStore : public ordo::core::Agent {
public:
    // cap: max entries retained per stack; a push beyond it evicts the oldest entry.
    explicit UndoStore(std::size_t cap = 100);

    // Pushes delta onto undoStack_ (evicting the oldest if over cap) and clears redoStack_.
    // Dispatches events::UndoStateChanged if canUndo()/canRedo() actually changed.
    void push(TransactionDelta delta);

    bool canUndo() const;
    bool canRedo() const;

    // Pops the top of undoStack_ and pushes a copy onto redoStack_; the caller applies the popped
    // delta BACKWARD (before-images). std::nullopt if undoStack_ was empty.
    // Dispatches events::UndoStateChanged if canUndo()/canRedo() actually changed.
    std::optional<TransactionDelta> takeUndo();

    // Symmetric to takeUndo(): pops redoStack_, pushes a copy onto undoStack_ (no cap eviction),
    // returns the popped delta to apply FORWARD (after-images). std::nullopt if redoStack_ was empty.
    std::optional<TransactionDelta> takeRedo();

    // Empties both stacks (New/Open document -- undo never crosses file boundaries).
    // Dispatches events::UndoStateChanged if canUndo()/canRedo() actually changed.
    void clearAll();

    // -- Aux-touch relay -----------------------------------------------------
    // Pinged unconditionally by the *Changed->dirty composition call sites, which can't tell
    // WHICH aux agent changed -- marks EVERY category touched (commit()'s own diff still excludes
    // anything unchanged). No-op if no transaction active.
    void notifyMutation();

    // Set by TransactionManager::begin(), cleared by its commit()/abort() -- UndoStore is the
    // reachable-from-anywhere relay point since TransactionManager is stack-local, not an Agent.
    // Overwrites any previously active transaction (last-wins); this app never nests transactions.
    void setActiveTransaction(TransactionManager* txn);

private:
    // Sends events::UndoStateChanged if canUndo()/canRedo() differ from the
    // before-snapshot passed in -- shared by every mutator above that can
    // change either stack's emptiness.
    void dispatchIfStateChanged(bool undoBefore, bool redoBefore);

    std::vector<TransactionDelta> undoStack_;
    std::vector<TransactionDelta> redoStack_;
    std::size_t cap_;

    TransactionManager* activeTransaction_ = nullptr;  // non-owning, nullable; see setActiveTransaction above
};

}  // namespace plnr::agent
