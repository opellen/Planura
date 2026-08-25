#pragma once

#include <string>
#include <string_view>

#include <ordo/core/app_kernel.h>
#include <ordo/core/agent.h>

#include "agent/events.h"
#include "agent/undo_store.h"

namespace plnr::agent {

inline constexpr std::string_view kDocumentStoreName = "document";

// Owns the open document's identity/dirty state. filePath empty means unsaved "Untitled".
// units is fixed at "in" for this milestone -- no mutator yet.
class DocumentStore : public ordo::core::Agent {
public:
    DocumentStore();

    // Sets dirty_ true. Dispatches events::DocumentStateChanged only on a false -> true transition.
    void markDirty();

    // Sets filePath_ = path and dirty_ = false (a completed save). Unlike markDirty, this always
    // dispatches: it's a direct user action, so the title bar must re-read the filename even when
    // dirty_ was already false (e.g. a Save As of an unchanged document).
    void setSaved(std::string path);

    // Clears filePath_ and dirty_ back to the unsaved "Untitled" state. Always dispatches, same
    // reasoning as setSaved.
    void resetNew();

    const std::string& filePath() const;
    bool dirty() const;
    const std::string& units() const;

private:
    std::string filePath_;
    bool dirty_ = false;
    std::string units_{"in"};
};

// Tiny reusable Command: marks DocumentStore dirty in reaction to EventT, then pings
// UndoStore::notifyMutation(). Instantiated per *Changed fact with no dedicated Command;
// GeometryChanged is the exception (already has GeometryChangedCommand -- registerCommand is
// single-slot per event type).
template <typename EventT>
class MarkDirtyCommand : public ordo::core::Command<EventT> {
public:
    void execute(ordo::core::AppKernel& kernel, const EventT& /*event*/) override {
        auto document = kernel.agentAs<DocumentStore>(kDocumentStoreName);
        if (document) document->markDirty();

        auto undo = kernel.agentAs<UndoStore>(kUndoStoreName);
        if (undo) undo->notifyMutation();
    }
};

}  // namespace plnr::agent
