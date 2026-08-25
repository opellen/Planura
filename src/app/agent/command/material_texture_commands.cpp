#include "agent/command/material_texture_commands.h"

#include <string>

#include <QByteArray>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QString>

#include <ordo/core/app_kernel.h>

#include "agent/asset_repository.h"
#include "agent/material_repository.h"

namespace plnr::agent {

namespace {

// Same report+status-hint convention as document_commands.cpp/
// obj_commands.cpp's own reportIoFailure -- duplicated rather than shared
// (no common .cpp between these standalone app-side command files).
void reportIoFailure(ordo::core::AppKernel& kernel, const std::string& message) {
    kernel.send(events::DocumentIoFailed{message});
    kernel.send(events::StatusHintChanged{message});
}

// Lowercase, dot-stripped extension from path's suffix, or empty if none.
// QFileInfo::suffix() already strips the dot and returns only the LAST
// extension.
std::string extensionOf(const QString& path) {
    return QFileInfo(path).suffix().toLower().toStdString();
}

}  // namespace

void MaterialSetTextureCommand::execute(ordo::core::AppKernel& kernel,
                                         const events::MaterialSetTextureRequested& event) {
    auto materials = kernel.agentAs<MaterialRepository>(kMaterialRepositoryName);
    auto assets = kernel.agentAs<AssetRepository>(kAssetRepositoryName);
    if (!materials || !assets) {
        return;  // One or more required Agents absent on this kernel.
    }

    if (event.path.empty()) {
        materials->clearTexture(event.materialId);
        return;
    }

    const QString path = QString::fromStdString(event.path);
    const std::string ext = extensionOf(path);
    if (ext != "png" && ext != "jpg" && ext != "jpeg") {
        reportIoFailure(kernel, "Unsupported texture image type (expected .png/.jpg/.jpeg): " + event.path);
        return;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        reportIoFailure(kernel, "Could not open texture image file: " + event.path);
        return;
    }
    const QByteArray bytes = file.readAll();
    file.close();

    const std::string hash = assets->add(bytes.toStdString(), ext);
    materials->setTexture(event.materialId, hash, event.tileW, event.tileH);
}

}  // namespace plnr::agent
