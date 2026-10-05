#include "agent/command/material_texture_commands.h"

#include <string>

#include <QByteArray>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QString>

#include <ordo/core/kernel.h>

#include "agent/asset_repository.h"
#include "agent/material_repository.h"

namespace plnr::agent {

namespace {

// Same report+status-hint convention as document_commands.cpp/
// obj_commands.cpp's own reportIoFailure -- duplicated rather than shared
// (no common .cpp between these standalone app-side command files).
void reportIoFailure(ordo::core::CommandContext& context, const std::string& message) {
    context.send(events::DocumentIoFailed{message});
    context.send(events::StatusHintChanged{message});
}

// Lowercase, dot-stripped extension from path's suffix, or empty if none.
// QFileInfo::suffix() already strips the dot and returns only the LAST
// extension.
std::string extensionOf(const QString& path) {
    return QFileInfo(path).suffix().toLower().toStdString();
}

}  // namespace

void MaterialSetTextureCommand::execute(const events::MaterialSetTextureRequested& event, ordo::core::CommandContext& context) {
    auto materials = context.agentAs<MaterialRepository>(kMaterialRepositoryName);
    auto assets = context.agentAs<AssetRepository>(kAssetRepositoryName);
    if (!materials || !assets) {
        return;  // One or more required Agents absent on this context.
    }

    if (event.path.empty()) {
        materials->clearTexture(event.materialId);
        return;
    }

    const QString path = QString::fromStdString(event.path);
    const std::string ext = extensionOf(path);
    if (ext != "png" && ext != "jpg" && ext != "jpeg") {
        reportIoFailure(context, "Unsupported texture image type (expected .png/.jpg/.jpeg): " + event.path);
        return;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        reportIoFailure(context, "Could not open texture image file: " + event.path);
        return;
    }
    const QByteArray bytes = file.readAll();
    file.close();

    const std::string hash = assets->add(bytes.toStdString(), ext);
    materials->setTexture(event.materialId, hash, event.tileW, event.tileH);
}

}  // namespace plnr::agent
