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

class Transaction;

// Owns the undo/redo stacks of TransactionDelta. Capture itself (journals, aux-agent snapshots)
// is Transaction's job; UndoStore just holds the stacks and relays the mutation ping.
class UndoStore : public ordo::core::Agent {
public:
    // cap: max entries retained per stack; a push beyond it evicts the oldest entry.
    explicit UndoStore(std::size_t cap = 100);

    // Pushes delta onto undoStack_ (evicting the oldest if over cap) and clears redoStack_.
    // Dispatches UndoStateChanged only if canUndo()/canRedo() changed (same for takeUndo/clearAll).
    void push(TransactionDelta delta);

    bool canUndo() const;
    bool canRedo() const;

    // Pops the top of undoStack_ and pushes a copy onto redoStack_; the caller applies the popped
    // delta BACKWARD (before-images). std::nullopt if undoStack_ was empty.
    std::optional<TransactionDelta> takeUndo();

    // Symmetric to takeUndo(): pops redoStack_, pushes a copy onto undoStack_ (no cap eviction),
    // returns the popped delta to apply FORWARD (after-images). std::nullopt if redoStack_ was empty.
    std::optional<TransactionDelta> takeRedo();

    // Empties both stacks (New/Open document -- undo never crosses file boundaries).
    void clearAll();

    // -- Aux-touch relay -----------------------------------------------------
    // Pinged by the *Changed->dirty call sites, which can't tell WHICH aux agent changed: marks EVERY category
    // touched (commit()'s diff still excludes unchanged ones). No-op if no transaction active.
    void notifyMutation();

    // Set by Transaction::begin(), cleared by commit()/abort() (Transaction is stack-local, so UndoStore is the relay point).
    // Last-wins; this app never nests transactions.
    void setActiveTransaction(Transaction* txn);

private:
    // Sends UndoStateChanged if canUndo()/canRedo() differ from the before-snapshot.
    void dispatchIfStateChanged(bool undoBefore, bool redoBefore);

    std::vector<TransactionDelta> undoStack_;
    std::vector<TransactionDelta> redoStack_;
    std::size_t cap_;

    Transaction* activeTransaction_ = nullptr;  // non-owning, nullable
};

}  // namespace plnr::agent
