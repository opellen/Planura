#include "agent/undo_store.h"

#include <utility>

#include "agent/transaction_manager.h"

namespace plnr::agent {

UndoStore::UndoStore(std::size_t cap) : Agent(std::string(kUndoStoreName)), cap_(cap) {}

void UndoStore::push(TransactionDelta delta) {
    const bool undoBefore = canUndo();
    const bool redoBefore = canRedo();

    undoStack_.push_back(std::move(delta));
    if (undoStack_.size() > cap_) {
        undoStack_.erase(undoStack_.begin());  // evict oldest
    }
    redoStack_.clear();  // a newly committed mutation invalidates redo history

    dispatchIfStateChanged(undoBefore, redoBefore);
}

bool UndoStore::canUndo() const {
    return !undoStack_.empty();
}

bool UndoStore::canRedo() const {
    return !redoStack_.empty();
}

std::optional<TransactionDelta> UndoStore::takeUndo() {
    if (undoStack_.empty()) {
        return std::nullopt;  // nothing to undo -- caller should have checked canUndo()
    }

    const bool undoBefore = canUndo();
    const bool redoBefore = canRedo();

    TransactionDelta delta = std::move(undoStack_.back());
    undoStack_.pop_back();
    redoStack_.push_back(delta);  // the delta moves to the other stack -- see this method's own header comment

    dispatchIfStateChanged(undoBefore, redoBefore);
    return delta;
}

std::optional<TransactionDelta> UndoStore::takeRedo() {
    if (redoStack_.empty()) {
        return std::nullopt;  // nothing to redo -- caller should have checked canRedo()
    }

    const bool undoBefore = canUndo();
    const bool redoBefore = canRedo();

    TransactionDelta delta = std::move(redoStack_.back());
    redoStack_.pop_back();
    undoStack_.push_back(delta);  // no cap eviction on this transfer, same as the snapshot-era takeRedo()

    dispatchIfStateChanged(undoBefore, redoBefore);
    return delta;
}

void UndoStore::clearAll() {
    const bool undoBefore = canUndo();
    const bool redoBefore = canRedo();

    undoStack_.clear();
    redoStack_.clear();

    dispatchIfStateChanged(undoBefore, redoBefore);
}

void UndoStore::notifyMutation() {
    if (activeTransaction_ != nullptr) {
        activeTransaction_->markAllAuxTouched();
    }
}

void UndoStore::setActiveTransaction(TransactionManager* txn) {
    activeTransaction_ = txn;
}

void UndoStore::dispatchIfStateChanged(bool undoBefore, bool redoBefore) {
    if (canUndo() != undoBefore || canRedo() != redoBefore) {
        send(events::UndoStateChanged{});
    }
}

}  // namespace plnr::agent
