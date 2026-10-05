#pragma once

#include <ordo/core/command.h>

#include "agent/events.h"

// OBJ export/import commands. Need io::writeObj/readObj (plnr_io) plus Qt, so obj_commands.cpp
// compiles straight into the executable and test binaries, never into plnr_agent.
namespace plnr::agent {

// Writes the live GeometryApi/TagStore to OBJ atomically via QSaveFile. Read-only w.r.t.
// document state (export is not a save). On failure, dispatches DocumentIoFailed.
class ExportObjCommand : public ordo::core::Command<events::ExportObjRequested> {
public:
    void execute(const events::ExportObjRequested& event, ordo::core::CommandContext& context) override;
};

// Reads and parses event.path via io::readObj, then hands the mesh to GeometryApi::importMesh
// (the file's name becomes the new component's name). On failure, dispatches DocumentIoFailed.
class ImportObjCommand : public ordo::core::Command<events::ImportObjRequested> {
public:
    void execute(const events::ImportObjRequested& event, ordo::core::CommandContext& context) override;
};

// Async export, main-thread half: io::writeObj over the live agents,
// published as ObjBytesReady; the file write runs on a worker. Touches no document state.
class ExportObjSnapshotCommand : public ordo::core::Command<events::ExportObjSnapshotRequested> {
public:
    void execute(const events::ExportObjSnapshotRequested& event, ordo::core::CommandContext& context) override;
};

// Async import apply: hands the already-parsed event.payload to GeometryApi::importMesh with
// the same facts/undo behavior as ImportObjCommand (register under UndoCaptureCommand).
class ImportObjDataCommand : public ordo::core::Command<events::ImportObjDataReady> {
public:
    void execute(const events::ImportObjDataReady& event, ordo::core::CommandContext& context) override;
};

}  // namespace plnr::agent
