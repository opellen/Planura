#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include <ordo/core/kernel.h>
#include <ordo/core/agent.h>

#include "agent/events.h"
#include "agent/undo_store.h"

namespace plnr::agent {

inline constexpr std::string_view kDocumentStoreName = "document";

// Owns the open document's identity/dirty state. filePath empty means unsaved "Untitled".
// units is fixed at "in" -- no mutator yet.
class DocumentStore : public ordo::core::Agent {
public:
    DocumentStore();

    // Sets dirty_ true. Dispatches events::DocumentStateChanged only on a false -> true transition.
    void markDirty();

    // Sets filePath_ and clears dirty_. Always dispatches (a direct user action: the title bar must re-read
    // the filename even if dirty_ was already false).
    void setSaved(std::string path);

    // Async save with a stale snapshot (a mutation landed after it): records the path, leaves dirty_. Always dispatches.
    void setPathKeepDirty(std::string path);

    // Monotonic count of markDirty() calls (including already-dirty no-ops), so an async save can detect mutations since its snapshot.
    std::uint64_t revision() const;

    // Back to the unsaved "Untitled" state. Always dispatches.
    void resetNew();

    const std::string& filePath() const;
    bool dirty() const;
    const std::string& units() const;

private:
    std::string filePath_;
    bool dirty_ = false;
    std::uint64_t revision_ = 0;
    std::string units_{"in"};
};

// Marks DocumentStore dirty in reaction to EventT, then pings UndoStore::notifyMutation(). Instantiated per *Changed fact
// without a dedicated Command; GeometryChanged is the exception (registerCommand is single-slot per event type).
template <typename EventT>
class MarkDirtyCommand : public ordo::core::Command<EventT> {
public:
    void execute(const EventT& /*event*/, ordo::core::CommandContext& context) override {
        auto document = context.agentAs<DocumentStore>(kDocumentStoreName);
        if (document) document->markDirty();

        auto undo = context.agentAs<UndoStore>(kUndoStoreName);
        if (undo) undo->notifyMutation();
    }
};

}  // namespace plnr::agent
