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
    void execute(ordo::core::AppKernel& kernel, const events::ExportObjRequested& event) override;
};

// Reads and parses event.path via io::readObj, then hands the mesh to GeometryApi::importMesh
// (the file's name becomes the new component's name). On failure, dispatches DocumentIoFailed.
class ImportObjCommand : public ordo::core::Command<events::ImportObjRequested> {
public:
    void execute(ordo::core::AppKernel& kernel, const events::ImportObjRequested& event) override;
};

}  // namespace plnr::agent
