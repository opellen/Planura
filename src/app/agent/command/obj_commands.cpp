#include "agent/command/obj_commands.h"

#include <string>

#include <QByteArray>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QSaveFile>
#include <QString>

#include <ordo/core/app_kernel.h>

#include "agent/geometry_api.h"
#include "agent/tag_store.h"
#include "io/obj_reader.h"
#include "io/obj_writer.h"

namespace plnr::agent {

namespace {

// Same report+status-hint convention as document_commands.cpp's own
// reportIoFailure -- duplicated since the two files share no common .cpp
// (each a standalone translation unit, per this header's own placement comment).
void reportIoFailure(ordo::core::AppKernel& kernel, const std::string& message) {
    kernel.send(events::DocumentIoFailed{message});
    kernel.send(events::StatusHintChanged{message});
}

}  // namespace

void ExportObjCommand::execute(ordo::core::AppKernel& kernel, const events::ExportObjRequested& event) {
    auto geometry = kernel.agentAs<GeometryApi>(kGeometryApiName);
    auto tags = kernel.agentAs<TagStore>(kTagStoreName);
    if (!geometry || !tags) {
        return;  // One or more required Agents absent on this kernel.
    }

    const QByteArray bytes = io::writeObj(*geometry, *tags);

    // QSaveFile: writes to a temp file and atomically renames it over the
    // target on commit() -- same atomicity guarantee SaveDocumentCommand's
    // own .plr write relies on.
    QSaveFile file(QString::fromStdString(event.path));
    if (!file.open(QIODevice::WriteOnly)) {
        reportIoFailure(kernel, "Could not open file for writing: " + event.path);
        return;
    }
    file.write(bytes);
    if (!file.commit()) {
        reportIoFailure(kernel, "Could not save file: " + event.path);
        return;
    }
}

void ImportObjCommand::execute(ordo::core::AppKernel& kernel, const events::ImportObjRequested& event) {
    auto geometry = kernel.agentAs<GeometryApi>(kGeometryApiName);
    if (!geometry) {
        return;  // No GeometryApi registered on this kernel.
    }

    QFile file(QString::fromStdString(event.path));
    if (!file.open(QIODevice::ReadOnly)) {
        reportIoFailure(kernel, "Could not open file: " + event.path);
        return;
    }
    const QByteArray bytes = file.readAll();
    file.close();

    const io::ReadObjResult result = io::readObj(bytes);
    if (!result.ok) {
        reportIoFailure(kernel, result.error.toStdString());
        return;
    }

    // The new component's name comes from the OBJ file's own name, extension
    // stripped (completeBaseName strips only the LAST extension, e.g.
    // "my.model.obj" -> "my.model", leaving any other dot alone).
    const std::string name = QFileInfo(QString::fromStdString(event.path)).completeBaseName().toStdString();

    const geo::Id instanceId = geometry->importMesh(name, result.mesh.vertices, result.mesh.faces);
    if (instanceId == geo::kInvalidId) {
        // GeometryApi::importMesh's own guards (empty vertex list, or no
        // face with >= 3 vertices) -- a parseable but empty/unbuildable OBJ.
        reportIoFailure(kernel, "OBJ file has no importable geometry: " + event.path);
    }
    // importMesh already fired GeometryChanged on success -- dirty tracking
    // and undo capture (via main.cpp's UndoCaptureCommand wrapper) follow
    // automatically, same as every other mutating Intent.
}

}  // namespace plnr::agent
