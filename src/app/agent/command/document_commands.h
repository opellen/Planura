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
    void execute(const events::NewDocumentRequested& event, ordo::core::CommandContext& context) override;
};

// Orchestrates OpenDocumentRequested: io::readDocument into the live agents + CameraStore, then
// the same reset sequence as NewDocumentCommand, ending in DocumentStore::setSaved(path).
// A read/parse failure dispatches DocumentIoFailed and leaves every agent untouched.
class OpenDocumentCommand : public ordo::core::Command<events::OpenDocumentRequested> {
public:
    void execute(const events::OpenDocumentRequested& event, ordo::core::CommandContext& context) override;
};

// Orchestrates SaveDocumentRequested: io::writeDocument over the live agents + camera, written
// atomically to event.path (raw JSON or ZIP depending on texture refs). On success,
// DocumentStore::setSaved(path); on failure, dispatches DocumentIoFailed, leaving it untouched.
class SaveDocumentCommand : public ordo::core::Command<events::SaveDocumentRequested> {
public:
    void execute(const events::SaveDocumentRequested& event, ordo::core::CommandContext& context) override;
};

// Async Open, apply half: OpenDocumentCommand minus the file IO, fed by a worker-prepared io::OpenPayload.
// Same all-or-nothing/failure contract and setSaved(path) LAST ordering. The bytes-path OpenDocumentCommand stays for the debug bridge.
class OpenDocumentDataCommand : public ordo::core::Command<events::OpenDocumentDataReady> {
public:
    void execute(const events::OpenDocumentDataReady& event, ordo::core::CommandContext& context) override;
};

// Async Save, snapshot half: io::writeDocument + camera/meta + asset blobs + DocumentStore::revision(),
// published as DocumentSnapshotReady. Writes nothing and does not touch dirty state.
class SaveSnapshotCommand : public ordo::core::Command<events::SaveSnapshotRequested> {
public:
    void execute(const events::SaveSnapshotRequested& event, ordo::core::CommandContext& context) override;
};

// Async Save, commit half: setSaved(path) iff the document revision still equals the snapshot's,
// else setPathKeepDirty(path). SaveDocumentCommand (sync) stays for the bridge and exit paths.
class SaveCommittedCommand : public ordo::core::Command<events::SaveCommitted> {
public:
    void execute(const events::SaveCommitted& event, ordo::core::CommandContext& context) override;
};

}  // namespace plnr::agent
