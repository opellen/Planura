#include "agent/command/document_commands.h"

#include <memory>
#include <string>

#include <QByteArray>
#include <QDateTime>
#include <QFile>
#include <QIODevice>
#include <QJsonDocument>
#include <QSaveFile>
#include <QString>

#include <ordo/core/app_kernel.h>

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

// Bundles the agents New/Open/Save each need a subset of, looked up once.
// ok() reports whether every field resolved (a test kernel that omits one
// just makes the owning command a no-op).
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
    // Own separate Agent -- see FogStore's own class comment.
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

DocumentStores lookupStores(ordo::core::AppKernel& kernel) {
    DocumentStores s;
    s.geometry = kernel.agentAs<GeometryApi>(kGeometryApiName);
    s.tags = kernel.agentAs<TagStore>(kTagStoreName);
    s.guides = kernel.agentAs<GuideStore>(kGuideStoreName);
    s.annotations = kernel.agentAs<AnnotationStore>(kAnnotationStoreName);
    s.sections = kernel.agentAs<SectionStore>(kSectionStoreName);
    s.axes = kernel.agentAs<AxesStore>(kAxesStoreName);
    s.materials = kernel.agentAs<MaterialRepository>(kMaterialRepositoryName);
    s.assets = kernel.agentAs<AssetRepository>(kAssetRepositoryName);
    s.style = kernel.agentAs<StyleStore>(kStyleStoreName);
    s.shadow = kernel.agentAs<ShadowStore>(kShadowStoreName);
    s.fog = kernel.agentAs<FogStore>(kFogStoreName);
    s.selection = kernel.agentAs<SelectionStore>(kSelectionStoreName);
    s.editContext = kernel.agentAs<EditContextStore>(kEditContextStoreName);
    s.camera = kernel.agentAs<CameraStore>(kCameraStoreName);
    s.document = kernel.agentAs<DocumentStore>(kDocumentStoreName);
    return s;
}

// Dispatches every refresh Fact once (New/Open share this exact sequence)
// so every presenter/renderer re-pulls -- CameraChanged/DocumentStateChanged
// are NOT among these (CameraStore/DocumentStore fire those themselves) and
// must run strictly AFTER, see the ordering comment at each call site.
void dispatchRefreshFacts(ordo::core::AppKernel& kernel) {
    kernel.send(events::GeometryChanged{});
    kernel.send(events::TagsChanged{});
    kernel.send(events::GuidesChanged{});
    kernel.send(events::AnnotationsChanged{});
    kernel.send(events::SectionsChanged{});
    kernel.send(events::AxesChanged{});
    kernel.send(events::MaterialsChanged{});
    kernel.send(events::StyleChanged{});
    kernel.send(events::ShadowsChanged{});
    kernel.send(events::FogChanged{});
    kernel.send(events::SelectionChanged{});
    kernel.send(events::EditContextChanged{});
}

void reportIoFailure(ordo::core::AppKernel& kernel, const std::string& message) {
    kernel.send(events::DocumentIoFailed{message});
    // StatusHintChanged reuses StatusBarPresenter's existing subscription to
    // surface the error; DocumentIoFailed is also dispatched for future
    // consumers -- see that event's own comment.
    kernel.send(events::StatusHintChanged{message});
}

}  // namespace

void NewDocumentCommand::execute(ordo::core::AppKernel& kernel, const events::NewDocumentRequested& /*event*/) {
    DocumentStores s = lookupStores(kernel);
    if (!s.ok()) return;  // One or more required Agents absent on this kernel.

    // clearForRestore on the 6 collection-shaped file-format agents.
    // AxesStore has no clearForRestore (a single Frame has no distinct
    // "empty" state) -- reset() to the world default is its equivalent.
    s.geometry->clearForRestore();
    s.tags->clearForRestore();
    s.tags->restoreTag(Tag{kUntaggedTagId, "Untagged", true});  // clearForRestore empties even the seeded default
    s.guides->clearForRestore();
    s.annotations->clearForRestore();
    s.sections->clearForRestore();
    s.axes->reset();
    s.materials->clearForRestore();  // no re-seed -- MaterialRepository has no default material to begin with
    s.assets->clearForRestore();     // no re-seed either, same reasoning
    s.style->clearForRestore();      // resets to the ctor-seeded default style
    s.shadow->clearForRestore();     // resets to the ctor-seeded default shadow settings
    s.fog->clearForRestore();        // resets to the ctor-seeded default fog settings

    s.selection->clear();
    s.editContext->reset();
    s.camera->restoreCamera(CameraState{});  // startup default -- fires CameraChanged

    // the reference modeler does not undo across file boundaries -- starts with empty
    // undo/redo history. Looked up separately from DocumentStores (not part
    // of its ok() check), so a kernel without UndoStore is unaffected.
    auto undo = kernel.agentAs<UndoStore>(kUndoStoreName);
    if (undo) undo->clearAll();

    dispatchRefreshFacts(kernel);

    // LAST: the refresh Facts just dispatched mark DocumentStore dirty via
    // the dirty-tracking composition; resetNew() runs strictly after
    // (sound since Dispatcher::send() is synchronous).
    s.document->resetNew();
}

void OpenDocumentCommand::execute(ordo::core::AppKernel& kernel, const events::OpenDocumentRequested& event) {
    DocumentStores s = lookupStores(kernel);
    if (!s.ok()) return;  // One or more required Agents absent on this kernel.

    QFile file(QString::fromStdString(event.path));
    if (!file.open(QIODevice::ReadOnly)) {
        reportIoFailure(kernel, "Could not open file: " + event.path);
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
        // All-or-nothing (io::readDocument's own contract): every agent is
        // still exactly as it was before this call. DocumentStore is left
        // untouched too -- no setSaved()/markDirty() call on this path.
        reportIoFailure(kernel, result.error.toStdString());
        return;
    }

    s.camera->restoreCamera(CameraState{cameraOut.target, cameraOut.azimuthDeg, cameraOut.elevationDeg,
                                         cameraOut.distance, cameraOut.fovYDeg, cameraOut.projection});
    s.editContext->reset();
    s.selection->clear();

    // Success path only -- a failed read already returned above without
    // touching any agent. Same null-agent-safety as NewDocumentCommand's own comment.
    auto undo = kernel.agentAs<UndoStore>(kUndoStoreName);
    if (undo) undo->clearAll();

    dispatchRefreshFacts(kernel);

    // LAST -- same ordering rationale as NewDocumentCommand's own comment:
    // refresh Facts mark DocumentStore dirty; setSaved() runs after and
    // clears it, landing on "path set, clean".
    s.document->setSaved(event.path);
}

void SaveDocumentCommand::execute(ordo::core::AppKernel& kernel, const events::SaveDocumentRequested& event) {
    DocumentStores s = lookupStores(kernel);
    if (!s.ok()) return;  // One or more required Agents absent on this kernel.

    const CameraState& cam = s.camera->state();
    const io::CameraState cameraState{cam.target, cam.azimuthDeg, cam.elevationDeg, cam.distance, cam.fovYDeg,
                                       cam.projection};
    const io::DocumentMeta meta{QString::fromStdString(std::string(kAppVersion)),
                                 QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs),
                                 QString::fromStdString(s.document->units())};

    const QJsonDocument doc = io::writeDocument(*s.geometry, *s.tags, *s.guides, *s.annotations, *s.sections, *s.axes,
                                                 *s.materials, *s.style, *s.shadow, *s.fog, cameraState, meta);
    // assembleContainer decides raw-JSON vs ZIP: doc.toJson(Indented)
    // verbatim when no material references a texture, a ZIP
    // (model.json + assets/<hash>.<ext>) otherwise -- see io/plr_container.h.
    const QByteArray bytes = io::assembleContainer(doc, *s.assets);

    // QSaveFile: writes to a temp file and atomically renames it over the
    // target on commit() -- a failure mid-write never corrupts whatever was
    // already saved at event.path.
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

    s.document->setSaved(event.path);
}

}  // namespace plnr::agent
