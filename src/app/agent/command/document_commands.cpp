#include "agent/command/document_commands.h"

#include <memory>
#include <string>
#include <utility>

#include <QByteArray>
#include <QDateTime>
#include <QFile>
#include <QIODevice>
#include <QJsonDocument>
#include <QSaveFile>
#include <QString>

#include <ordo/core/kernel.h>

#include "app_version.h"
#include "agent/annotation_store.h"
#include "agent/asset_repository.h"
#include "agent/axes_store.h"
#include "agent/camera_store.h"
#include "agent/document_store.h"
#include "agent/edit_context_store.h"
#include "agent/fog_store.h"
#include "agent/geometry_api.h"
#include "agent/guide_store.h"
#include "agent/material_repository.h"
#include "agent/section_store.h"
#include "agent/selection_store.h"
#include "agent/shadow_store.h"
#include "agent/style_store.h"
#include "agent/tag_store.h"
#include "agent/undo_store.h"
#include "io/plr_container.h"
#include "io/plr_reader.h"
#include "io/plr_writer.h"

namespace plnr::agent {

namespace {

// Agents New/Open/Save each need, looked up once. ok() false (e.g. a test context missing one) makes the command a no-op.
struct DocumentStores {
    std::shared_ptr<GeometryApi> geometry;
    std::shared_ptr<TagStore> tags;
    std::shared_ptr<GuideStore> guides;
    std::shared_ptr<AnnotationStore> annotations;
    std::shared_ptr<SectionStore> sections;
    std::shared_ptr<AxesStore> axes;
    std::shared_ptr<MaterialRepository> materials;
    std::shared_ptr<AssetRepository> assets;
    std::shared_ptr<StyleStore> style;
    std::shared_ptr<ShadowStore> shadow;
    std::shared_ptr<FogStore> fog;
    std::shared_ptr<SelectionStore> selection;
    std::shared_ptr<EditContextStore> editContext;
    std::shared_ptr<CameraStore> camera;
    std::shared_ptr<DocumentStore> document;

    bool ok() const {
        return geometry && tags && guides && annotations && sections && axes && materials && assets && style &&
               shadow && fog && selection && editContext && camera && document;
    }
};

DocumentStores lookupStores(ordo::core::CommandContext& context) {
    DocumentStores s;
    s.geometry = context.agentAs<GeometryApi>(kGeometryApiName);
    s.tags = context.agentAs<TagStore>(kTagStoreName);
    s.guides = context.agentAs<GuideStore>(kGuideStoreName);
    s.annotations = context.agentAs<AnnotationStore>(kAnnotationStoreName);
    s.sections = context.agentAs<SectionStore>(kSectionStoreName);
    s.axes = context.agentAs<AxesStore>(kAxesStoreName);
    s.materials = context.agentAs<MaterialRepository>(kMaterialRepositoryName);
    s.assets = context.agentAs<AssetRepository>(kAssetRepositoryName);
    s.style = context.agentAs<StyleStore>(kStyleStoreName);
    s.shadow = context.agentAs<ShadowStore>(kShadowStoreName);
    s.fog = context.agentAs<FogStore>(kFogStoreName);
    s.selection = context.agentAs<SelectionStore>(kSelectionStoreName);
    s.editContext = context.agentAs<EditContextStore>(kEditContextStoreName);
    s.camera = context.agentAs<CameraStore>(kCameraStoreName);
    s.document = context.agentAs<DocumentStore>(kDocumentStoreName);
    return s;
}

// Sends every refresh Fact once (New/Open share it). CameraChanged/DocumentStateChanged are NOT among them
// (the stores fire those) and must run strictly AFTER.
void dispatchRefreshFacts(ordo::core::CommandContext& context) {
    context.send(events::GeometryChanged{});
    context.send(events::TagsChanged{});
    context.send(events::GuidesChanged{});
    context.send(events::AnnotationsChanged{});
    context.send(events::SectionsChanged{});
    context.send(events::AxesChanged{});
    context.send(events::MaterialsChanged{});
    context.send(events::StyleChanged{});
    context.send(events::ShadowsChanged{});
    context.send(events::FogChanged{});
    context.send(events::SelectionChanged{});
    context.send(events::EditContextChanged{});
}

void reportIoFailure(ordo::core::CommandContext& context, const std::string& message) {
    context.send(events::DocumentIoFailed{message});
    // StatusHintChanged surfaces the error via StatusBarPresenter's existing subscription.
    context.send(events::StatusHintChanged{message});
}

// Post-apply tail shared by the Open commands; runs only after io::readDocument succeeded.
void finishOpen(DocumentStores& s, ordo::core::CommandContext& context, const io::CameraState& cameraOut,
                const std::string& path) {
    s.camera->restoreCamera(CameraState{cameraOut.target, cameraOut.azimuthDeg, cameraOut.elevationDeg,
                                         cameraOut.distance, cameraOut.fovYDeg, cameraOut.projection});
    s.editContext->reset();
    s.selection->clear();

    auto undo = context.agentAs<UndoStore>(kUndoStoreName);
    if (undo) undo->clearAll();

    dispatchRefreshFacts(context);

    // LAST: refresh Facts mark DocumentStore dirty; setSaved() afterwards clears it (path set, clean).
    s.document->setSaved(path);
}

// Serializes live agents + camera/meta into the .plr JSON (main-thread half of every save path).
QJsonDocument captureDocument(DocumentStores& s) {
    const CameraState& cam = s.camera->state();
    const io::CameraState cameraState{cam.target, cam.azimuthDeg, cam.elevationDeg, cam.distance, cam.fovYDeg,
                                       cam.projection};
    const io::DocumentMeta meta{QString::fromStdString(std::string(kAppVersion)),
                                 QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs),
                                 QString::fromStdString(s.document->units())};

    return io::writeDocument(*s.geometry, *s.tags, *s.guides, *s.annotations, *s.sections, *s.axes, *s.materials,
                              *s.style, *s.shadow, *s.fog, cameraState, meta);
}

}  // namespace

void NewDocumentCommand::execute(const events::NewDocumentRequested& /*event*/, ordo::core::CommandContext& context) {
    DocumentStores s = lookupStores(context);
    if (!s.ok()) return;

    // AxesStore has no clearForRestore (a single Frame has no "empty" state); reset() is its equivalent.
    s.geometry->clearForRestore();
    s.tags->clearForRestore();
    s.tags->restoreTag(Tag{kUntaggedTagId, "Untagged", true});  // clearForRestore empties even the seeded default
    s.guides->clearForRestore();
    s.annotations->clearForRestore();
    s.sections->clearForRestore();
    s.axes->reset();
    s.materials->clearForRestore();  // no default material to re-seed
    s.assets->clearForRestore();
    s.style->clearForRestore();      // resets to the ctor-seeded default style
    s.shadow->clearForRestore();     // resets to the ctor-seeded default shadow settings
    s.fog->clearForRestore();        // resets to the ctor-seeded default fog settings

    s.selection->clear();
    s.editContext->reset();
    s.camera->restoreCamera(CameraState{});  // startup default -- fires CameraChanged

    // No undo across file boundaries: history starts empty. Not in DocumentStores, so a context without UndoStore is unaffected.
    auto undo = context.agentAs<UndoStore>(kUndoStoreName);
    if (undo) undo->clearAll();

    dispatchRefreshFacts(context);

    // LAST: the refresh Facts mark DocumentStore dirty; resetNew() must run strictly after (Dispatcher::send() is synchronous).
    s.document->resetNew();
}

void OpenDocumentCommand::execute(const events::OpenDocumentRequested& event, ordo::core::CommandContext& context) {
    DocumentStores s = lookupStores(context);
    if (!s.ok()) return;

    QFile file(QString::fromStdString(event.path));
    if (!file.open(QIODevice::ReadOnly)) {
        reportIoFailure(context, "Could not open file: " + event.path);
        return;
    }
    const QByteArray bytes = file.readAll();
    file.close();

    io::CameraState cameraOut;
    io::DocumentMeta metaOut;
    const io::ReadResult result = io::readDocument(bytes, *s.geometry, *s.tags, *s.guides, *s.annotations,
                                                     *s.sections, *s.axes, *s.materials, *s.assets, *s.style,
                                                     *s.shadow, *s.fog, &cameraOut, &metaOut);
    if (!result.ok) {
        // All-or-nothing (io::readDocument's contract): agents and DocumentStore untouched, no setSaved()/markDirty().
        reportIoFailure(context, result.error.toStdString());
        return;
    }

    finishOpen(s, context, cameraOut, event.path);
}

void OpenDocumentDataCommand::execute(const events::OpenDocumentDataReady& event,
                                      ordo::core::CommandContext& context) {
    DocumentStores s = lookupStores(context);
    if (!s.ok() || !event.payload) return;

    // File IO, container extraction and JSON parsing already ran on the worker; same io::readDocument core as OpenDocumentCommand.
    io::CameraState cameraOut;
    io::DocumentMeta metaOut;
    io::OpenPayload& payload = *event.payload;
    const io::ReadResult result =
        io::readDocument(payload.model, std::move(payload.assets), payload.fromContainer, *s.geometry, *s.tags,
                         *s.guides, *s.annotations, *s.sections, *s.axes, *s.materials, *s.assets, *s.style,
                         *s.shadow, *s.fog, &cameraOut, &metaOut);
    if (!result.ok) {
        reportIoFailure(context, result.error.toStdString());
        return;
    }

    finishOpen(s, context, cameraOut, event.path);
}

void SaveSnapshotCommand::execute(const events::SaveSnapshotRequested& event, ordo::core::CommandContext& context) {
    DocumentStores s = lookupStores(context);
    if (!s.ok()) return;

    auto snapshot = std::make_shared<io::SaveSnapshot>();
    snapshot->doc = captureDocument(s);
    snapshot->blobs = io::collectAssetBlobs(*s.assets);
    // Same synchronous step as the document value: later mutations bump revision() past it (SaveCommittedCommand reads that as stale).
    context.send(events::DocumentSnapshotReady{event.path, std::move(snapshot), s.document->revision()});
}

void SaveCommittedCommand::execute(const events::SaveCommitted& event, ordo::core::CommandContext& context) {
    auto document = context.agentAs<DocumentStore>(kDocumentStoreName);
    if (!document) return;

    // Edits made while the worker wrote are NOT in the file: stay dirty, record only the path.
    if (event.revision == document->revision()) {
        document->setSaved(event.path);
    } else {
        document->setPathKeepDirty(event.path);
    }
}

void SaveDocumentCommand::execute(const events::SaveDocumentRequested& event, ordo::core::CommandContext& context) {
    DocumentStores s = lookupStores(context);
    if (!s.ok()) return;

    const QJsonDocument doc = captureDocument(s);
    // assembleContainer: raw JSON, or ZIP when a material references a texture.
    const QByteArray bytes = io::assembleContainer(doc, *s.assets);

    // QSaveFile commits atomically: a mid-write failure never corrupts the previously saved file.
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

    s.document->setSaved(event.path);
}

}  // namespace plnr::agent
