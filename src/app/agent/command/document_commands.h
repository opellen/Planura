#pragma once

#include <ordo/core/command.h>

#include "agent/events.h"

// New/Open/Save document commands. Unlike other agent/*_commands.h pairs, this .cpp needs Qt +
// plnr_io, so it compiles straight into the executable and test binaries, never into plnr_agent.
namespace plnr::agent {

// Orchestrates NewDocumentRequested: resets every file-format agent, TagStore reseeds Untagged,
// resets Selection/EditContext/Camera, clears UndoStore, dispatches every refresh Fact, then
// DocumentStore::resetNew() LAST -- ordering matters for dirty tracking.
class NewDocumentCommand : public ordo::core::Command<events::NewDocumentRequested> {
public:
    void execute(ordo::core::AppKernel& kernel, const events::NewDocumentRequested& event) override;
};

// Orchestrates OpenDocumentRequested: io::readDocument into the live agents + CameraStore, then
// the same reset sequence as NewDocumentCommand, ending in DocumentStore::setSaved(path).
// A read/parse failure dispatches DocumentIoFailed and leaves every agent untouched.
class OpenDocumentCommand : public ordo::core::Command<events::OpenDocumentRequested> {
public:
    void execute(ordo::core::AppKernel& kernel, const events::OpenDocumentRequested& event) override;
};

// Orchestrates SaveDocumentRequested: io::writeDocument over the live agents + camera, written
// atomically to event.path (raw JSON or ZIP depending on texture refs). On success,
// DocumentStore::setSaved(path); on failure, dispatches DocumentIoFailed, leaving it untouched.
class SaveDocumentCommand : public ordo::core::Command<events::SaveDocumentRequested> {
public:
    void execute(ordo::core::AppKernel& kernel, const events::SaveDocumentRequested& event) override;
};

}  // namespace plnr::agent
