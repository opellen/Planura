#include "agent/command/obj_commands.h"

#include <memory>
#include <string>

#include <QByteArray>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QSaveFile>
#include <QString>

#include <ordo/core/kernel.h>

#include "agent/geometry_api.h"
#include "agent/tag_store.h"
#include "io/obj_reader.h"
#include "io/obj_writer.h"

namespace plnr::agent {

namespace {

// Same report + status-hint convention as document_commands.cpp (separate translation units, so duplicated).
void reportIoFailure(ordo::core::CommandContext& context, const std::string& message) {
    context.send(events::DocumentIoFailed{message});
    context.send(events::StatusHintChanged{message});
}

// Apply tail shared by ImportObjCommand and ImportObjDataCommand; runs only after the OBJ parsed ok.
void applyImport(GeometryApi& geometry, ordo::core::CommandContext& context, const std::string& name,
                 const io::ObjMesh& mesh, const std::string& path) {
    const geo::Id instanceId = geometry.importMesh(name, mesh.vertices, mesh.faces);
    if (instanceId == geo::kInvalidId) {
        // importMesh's guards: empty vertex list, or no face with >= 3 vertices.
        reportIoFailure(context, "OBJ file has no importable geometry: " + path);
    }
    // importMesh already fired GeometryChanged: dirty tracking and undo capture follow automatically.
}

}  // namespace

void ExportObjCommand::execute(const events::ExportObjRequested& event, ordo::core::CommandContext& context) {
    auto geometry = context.agentAs<GeometryApi>(kGeometryApiName);
    auto tags = context.agentAs<TagStore>(kTagStoreName);
    if (!geometry || !tags) {
                return;
    }

    const QByteArray bytes = io::writeObj(*geometry, *tags);

    // QSaveFile commits atomically, like the .plr save.
    QSaveFile file(QString::fromStdString(event.path));
    if (!file.open(QIODevice::WriteOnly)) {
        reportIoFailure(context, "Could not open file for writing: " + event.path);
        return;
    }
    file.write(bytes);
    if (!file.commit()) {
        reportIoFailure(context, "Could not save file: " + event.path);
        return;
    }
    // Export is not a save: no DocumentStore touch (no dirty flag, no setSaved).
}

void ImportObjCommand::execute(const events::ImportObjRequested& event, ordo::core::CommandContext& context) {
    auto geometry = context.agentAs<GeometryApi>(kGeometryApiName);
    if (!geometry) {
                return;
    }

    QFile file(QString::fromStdString(event.path));
    if (!file.open(QIODevice::ReadOnly)) {
        reportIoFailure(context, "Could not open file: " + event.path);
        return;
    }
    const QByteArray bytes = file.readAll();
    file.close();

    const io::ReadObjResult result = io::readObj(bytes);
    if (!result.ok) {
        reportIoFailure(context, result.error.toStdString());
        return;
    }

    // Component name = file name minus its LAST extension only (completeBaseName).
    const std::string name = QFileInfo(QString::fromStdString(event.path)).completeBaseName().toStdString();

    applyImport(*geometry, context, name, result.mesh, event.path);
}

void ImportObjDataCommand::execute(const events::ImportObjDataReady& event, ordo::core::CommandContext& context) {
    auto geometry = context.agentAs<GeometryApi>(kGeometryApiName);
    if (!geometry || !event.payload) return;

    // File IO and parsing already ran on the worker.
    applyImport(*geometry, context, event.name, event.payload->mesh, event.path);
}

void ExportObjSnapshotCommand::execute(const events::ExportObjSnapshotRequested& event,
                                       ordo::core::CommandContext& context) {
    auto geometry = context.agentAs<GeometryApi>(kGeometryApiName);
    auto tags = context.agentAs<TagStore>(kTagStoreName);
    if (!geometry || !tags) return;

    context.send(events::ObjBytesReady{
        event.path, std::make_shared<const QByteArray>(io::writeObj(*geometry, *tags))});
}

}  // namespace plnr::agent
