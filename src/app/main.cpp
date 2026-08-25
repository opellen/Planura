// Application entry point: builds the Ordo kernel and the industry-standard
// main window shell, then hands control to the Qt event loop.

#include <cstring>
#include <memory>

#include <QApplication>
#include <QString>
#include <QTimer>
#include <QtGlobal>

#include <ordo/core/app_kernel.h>

#include "devbridge/debug_bridge.h"
#include "agent/command/annotation_commands.h"
#include "agent/annotation_store.h"
#include "agent/asset_repository.h"
#include "agent/command/axes_commands.h"
#include "agent/axes_store.h"
#include "agent/camera_store.h"
#include "agent/command/document_commands.h"
#include "agent/document_store.h"
#include "agent/command/edit_context_commands.h"
#include "agent/edit_context_store.h"
#include "agent/events.h"
#include "agent/command/geometry_commands.h"
#include "agent/geometry_api.h"
#include "agent/command/group_commands.h"
#include "agent/command/guide_commands.h"
#include "agent/guide_store.h"
#include "agent/command/material_commands.h"
#include "agent/material_repository.h"
#include "agent/command/material_texture_commands.h"
#include "agent/command/obj_commands.h"
#include "agent/command/section_commands.h"
#include "agent/section_store.h"
#include "agent/command/selection_commands.h"
#include "agent/selection_store.h"
#include "agent/command/fog_commands.h"
#include "agent/fog_store.h"
#include "agent/command/shadow_commands.h"
#include "agent/shadow_store.h"
#include "agent/command/solid_commands.h"
#include "agent/command/style_commands.h"
#include "agent/style_store.h"
#include "agent/command/tag_commands.h"
#include "agent/tag_store.h"
#include "agent/command/undo_commands.h"
#include "agent/undo_store.h"
#include "main_window.h"
#include "viewport/viewport_widget.h"

namespace {

// `--screenshot <path>`: grabs the viewport framebuffer shortly after show() and exits.
QString screenshotPathFromArgs(int argc, char* argv[]) {
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::strcmp(argv[i], "--screenshot") == 0) {
            return QString::fromLocal8Bit(argv[i + 1]);
        }
    }
    return QString();
}

// Simple argv boolean-flag check.
bool hasFlag(int argc, char* argv[], const char* flag) {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], flag) == 0) return true;
    }
    return false;
}

// `--debug-bridge [port]`: starts the in-app TCP JSON debug bridge. Port
// resolution order: explicit arg -> PLNR_BRIDGE_PORT env var -> kDefaultPort.
// Returns 0 (no bridge) when the flag is absent.
quint16 debugBridgePortFromArgs(int argc, char* argv[]) {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--debug-bridge") == 0) {
            if (i + 1 < argc) {
                bool ok = false;
                const int port = QString::fromLocal8Bit(argv[i + 1]).toInt(&ok);
                if (ok && port > 0 && port <= 65535) {
                    return static_cast<quint16>(port);
                }
            }
            const int envPort = qEnvironmentVariableIntValue("PLNR_BRIDGE_PORT");
            if (envPort > 0 && envPort <= 65535) {
                return static_cast<quint16>(envPort);
            }
            return plnr::devbridge::DebugBridge::kDefaultPort;
        }
    }
    return 0;
}

}  // namespace

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

    ordo::core::AppKernel kernel;

    // Bootstrap order (policy §4): construct kernel -> register Agents ->
    // construct Presenters (inside MainWindow) -> registerCommands -> show.
    kernel.registerAgent(std::make_shared<plnr::agent::GeometryApi>());
    kernel.registerAgent(std::make_shared<plnr::agent::SelectionStore>());
    kernel.registerAgent(std::make_shared<plnr::agent::TagStore>());
    kernel.registerAgent(std::make_shared<plnr::agent::EditContextStore>());
    kernel.registerAgent(std::make_shared<plnr::agent::GuideStore>());
    kernel.registerAgent(std::make_shared<plnr::agent::AxesStore>());
    kernel.registerAgent(std::make_shared<plnr::agent::AnnotationStore>());
    kernel.registerAgent(std::make_shared<plnr::agent::SectionStore>());
    // MaterialRepository: in-model material list + per-entity paint assignments.
    kernel.registerAgent(std::make_shared<plnr::agent::MaterialRepository>());
    // AssetRepository/StyleStore/ShadowStore/FogStore must register before
    // document_commands.cpp's New/Open/Save (DocumentStores::ok() requires it).
    kernel.registerAgent(std::make_shared<plnr::agent::AssetRepository>());
    kernel.registerAgent(std::make_shared<plnr::agent::StyleStore>());
    kernel.registerAgent(std::make_shared<plnr::agent::ShadowStore>());
    kernel.registerAgent(std::make_shared<plnr::agent::FogStore>());
    kernel.registerAgent(std::make_shared<plnr::agent::DocumentStore>());
    kernel.registerAgent(std::make_shared<plnr::agent::CameraStore>());
    kernel.registerAgent(std::make_shared<plnr::agent::UndoStore>());

    plnr::MainWindow window(kernel);

    // Undo-capture rule: every mutating Intent below is wrapped in
    // UndoCaptureCommand<RealCommand, EventT> -- new mutating Intents must
    // be too, or they silently become un-undoable. Excluded: view-state-only
    // events (Select*, EnterContext/ExitContext, camera/projection, transient
    // UI state) and the *Changed Fact reactions themselves.
    kernel.registerCommand<plnr::events::AddEdgeRequested,
                            plnr::agent::UndoCaptureCommand<plnr::agent::AddEdgeCommand, plnr::events::AddEdgeRequested>>();
    kernel.registerCommand<
        plnr::events::AddRectangleRequested,
        plnr::agent::UndoCaptureCommand<plnr::agent::AddRectangleCommand, plnr::events::AddRectangleRequested>>();
    kernel.registerCommand<
        plnr::events::ExtrudeFaceRequested,
        plnr::agent::UndoCaptureCommand<plnr::agent::ExtrudeFaceCommand, plnr::events::ExtrudeFaceRequested>>();
    kernel.registerCommand<
        plnr::events::MoveEntityRequested,
        plnr::agent::UndoCaptureCommand<plnr::agent::MoveEntityCommand, plnr::events::MoveEntityRequested>>();
    kernel.registerCommand<
        plnr::events::RemoveEdgeRequested,
        plnr::agent::UndoCaptureCommand<plnr::agent::RemoveEdgeCommand, plnr::events::RemoveEdgeRequested>>();
    kernel.registerCommand<
        plnr::events::AddPolylineRequested,
        plnr::agent::UndoCaptureCommand<plnr::agent::AddPolylineCommand, plnr::events::AddPolylineRequested>>();
    kernel.registerCommand<
        plnr::events::Add3dTextRequested,
        plnr::agent::UndoCaptureCommand<plnr::agent::Add3dTextCommand, plnr::events::Add3dTextRequested>>();
    kernel.registerCommand<plnr::events::ReplaceLastPolylineRequested,
                            plnr::agent::UndoCaptureCommand<plnr::agent::ReplaceLastPolylineCommand,
                                                             plnr::events::ReplaceLastPolylineRequested>>();
    kernel.registerCommand<plnr::events::TransformEntitiesRequested,
                            plnr::agent::UndoCaptureCommand<plnr::agent::TransformEntitiesCommand,
                                                             plnr::events::TransformEntitiesRequested>>();
    kernel.registerCommand<
        plnr::events::ApplyArrayRequested,
        plnr::agent::UndoCaptureCommand<plnr::agent::ApplyArrayCommand, plnr::events::ApplyArrayRequested>>();
    kernel.registerCommand<plnr::events::FollowMeRequested,
                            plnr::agent::UndoCaptureCommand<plnr::agent::FollowMeCommand, plnr::events::FollowMeRequested>>();
    kernel.registerCommand<
        plnr::events::DivideEdgeRequested,
        plnr::agent::UndoCaptureCommand<plnr::agent::DivideEdgeCommand, plnr::events::DivideEdgeRequested>>();
    // Combines PruneSelectionCommand's reaction + AnnotationStore dimension
    // refresh (Ordo allows only one registered Command per event type).
    kernel.registerCommand<plnr::events::GeometryChanged, plnr::agent::GeometryChangedCommand>();
    // Select None is a default-constructed SelectRequested{}, already covered here.
    kernel.registerCommand<plnr::events::SelectRequested, plnr::agent::SelectCommand>();
    kernel.registerCommand<plnr::events::SelectRegionRequested, plnr::agent::SelectRegionCommand>();
    kernel.registerCommand<plnr::events::SelectAllRequested, plnr::agent::SelectAllCommand>();
    kernel.registerCommand<
        plnr::events::SetHiddenRequested,
        plnr::agent::UndoCaptureCommand<plnr::agent::SetHiddenCommand, plnr::events::SetHiddenRequested>>();
    kernel.registerCommand<
        plnr::events::UnhideAllRequested,
        plnr::agent::UndoCaptureCommand<plnr::agent::UnhideAllCommand, plnr::events::UnhideAllRequested>>();
    kernel.registerCommand<
        plnr::events::DeleteSelectionRequested,
        plnr::agent::UndoCaptureCommand<plnr::agent::DeleteSelectionCommand, plnr::events::DeleteSelectionRequested>>();
    kernel.registerCommand<
        plnr::events::GroupCreateRequested,
        plnr::agent::UndoCaptureCommand<plnr::agent::GroupCreateCommand, plnr::events::GroupCreateRequested>>();
    kernel.registerCommand<plnr::events::ExplodeRequested,
                            plnr::agent::UndoCaptureCommand<plnr::agent::ExplodeCommand, plnr::events::ExplodeRequested>>();
    kernel.registerCommand<plnr::events::EnterContextRequested, plnr::agent::EnterContextCommand>();
    kernel.registerCommand<plnr::events::ExitContextRequested, plnr::agent::ExitContextCommand>();
    kernel.registerCommand<
        plnr::events::TagCreateRequested,
        plnr::agent::UndoCaptureCommand<plnr::agent::TagCreateCommand, plnr::events::TagCreateRequested>>();
    kernel.registerCommand<
        plnr::events::TagAssignRequested,
        plnr::agent::UndoCaptureCommand<plnr::agent::TagAssignCommand, plnr::events::TagAssignRequested>>();
    kernel.registerCommand<
        plnr::events::TagVisibilityRequested,
        plnr::agent::UndoCaptureCommand<plnr::agent::TagVisibilityCommand, plnr::events::TagVisibilityRequested>>();
    kernel.registerCommand<
        plnr::events::AddGuideLineRequested,
        plnr::agent::UndoCaptureCommand<plnr::agent::AddGuideLineCommand, plnr::events::AddGuideLineRequested>>();
    kernel.registerCommand<
        plnr::events::AddGuidePointRequested,
        plnr::agent::UndoCaptureCommand<plnr::agent::AddGuidePointCommand, plnr::events::AddGuidePointRequested>>();
    kernel.registerCommand<
        plnr::events::EraseGuideRequested,
        plnr::agent::UndoCaptureCommand<plnr::agent::EraseGuideCommand, plnr::events::EraseGuideRequested>>();
    kernel.registerCommand<plnr::events::DeleteAllGuidesRequested,
                            plnr::agent::UndoCaptureCommand<plnr::agent::DeleteAllGuidesCommand,
                                                             plnr::events::DeleteAllGuidesRequested>>();
    kernel.registerCommand<
        plnr::events::SetGuideHiddenRequested,
        plnr::agent::UndoCaptureCommand<plnr::agent::SetGuideHiddenCommand, plnr::events::SetGuideHiddenRequested>>();
    kernel.registerCommand<plnr::events::SetAxesRequested,
                            plnr::agent::UndoCaptureCommand<plnr::agent::SetAxesCommand, plnr::events::SetAxesRequested>>();
    kernel.registerCommand<
        plnr::events::ResetAxesRequested,
        plnr::agent::UndoCaptureCommand<plnr::agent::ResetAxesCommand, plnr::events::ResetAxesRequested>>();
    kernel.registerCommand<
        plnr::events::AddDimensionRequested,
        plnr::agent::UndoCaptureCommand<plnr::agent::AddDimensionCommand, plnr::events::AddDimensionRequested>>();
    kernel.registerCommand<
        plnr::events::AddScreenTextRequested,
        plnr::agent::UndoCaptureCommand<plnr::agent::AddScreenTextCommand, plnr::events::AddScreenTextRequested>>();
    kernel.registerCommand<
        plnr::events::AddLeaderTextRequested,
        plnr::agent::UndoCaptureCommand<plnr::agent::AddLeaderTextCommand, plnr::events::AddLeaderTextRequested>>();
    kernel.registerCommand<plnr::events::SetAnnotationTextRequested,
                            plnr::agent::UndoCaptureCommand<plnr::agent::SetAnnotationTextCommand,
                                                             plnr::events::SetAnnotationTextRequested>>();
    kernel.registerCommand<
        plnr::events::RemoveAnnotationRequested,
        plnr::agent::UndoCaptureCommand<plnr::agent::RemoveAnnotationCommand, plnr::events::RemoveAnnotationRequested>>();
    kernel.registerCommand<plnr::events::RemoveAllAnnotationsRequested,
                            plnr::agent::UndoCaptureCommand<plnr::agent::RemoveAllAnnotationsCommand,
                                                             plnr::events::RemoveAllAnnotationsRequested>>();
    kernel.registerCommand<
        plnr::events::AddSectionPlaneRequested,
        plnr::agent::UndoCaptureCommand<plnr::agent::AddSectionPlaneCommand, plnr::events::AddSectionPlaneRequested>>();
    kernel.registerCommand<plnr::events::RemoveSectionPlaneRequested,
                            plnr::agent::UndoCaptureCommand<plnr::agent::RemoveSectionPlaneCommand,
                                                             plnr::events::RemoveSectionPlaneRequested>>();
    kernel.registerCommand<plnr::events::SetSectionActiveRequested,
                            plnr::agent::UndoCaptureCommand<plnr::agent::SetSectionActiveCommand,
                                                             plnr::events::SetSectionActiveRequested>>();
    kernel.registerCommand<
        plnr::events::ReverseSectionRequested,
        plnr::agent::UndoCaptureCommand<plnr::agent::ReverseSectionCommand, plnr::events::ReverseSectionRequested>>();
    kernel.registerCommand<plnr::events::SetSectionHiddenRequested,
                            plnr::agent::UndoCaptureCommand<plnr::agent::SetSectionHiddenCommand,
                                                             plnr::events::SetSectionHiddenRequested>>();

    kernel.registerCommand<
        plnr::events::MaterialCreateRequested,
        plnr::agent::UndoCaptureCommand<plnr::agent::MaterialCreateCommand, plnr::events::MaterialCreateRequested>>();
    kernel.registerCommand<
        plnr::events::MaterialEditRequested,
        plnr::agent::UndoCaptureCommand<plnr::agent::MaterialEditCommand, plnr::events::MaterialEditRequested>>();
    kernel.registerCommand<plnr::events::PaintRequested,
                            plnr::agent::UndoCaptureCommand<plnr::agent::PaintCommand, plnr::events::PaintRequested>>();
    // SetActiveMaterialRequested is transient UI state (not document content) -- not wrapped.
    kernel.registerCommand<plnr::events::SetActiveMaterialRequested, plnr::agent::SetActiveMaterialCommand>();

    // Undo reverts only the Material's {assetHash, tileW, tileH} fields;
    // the AssetRepository blob itself is never undo-captured.
    kernel.registerCommand<plnr::events::MaterialSetTextureRequested,
                            plnr::agent::UndoCaptureCommand<plnr::agent::MaterialSetTextureCommand,
                                                             plnr::events::MaterialSetTextureRequested>>();

    // Undo reverts the whole {materials, assignments} aux-diff snapshot,
    // which already carries uvTransform.
    kernel.registerCommand<
        plnr::events::SetUvTransformRequested,
        plnr::agent::UndoCaptureCommand<plnr::agent::SetUvTransformCommand, plnr::events::SetUvTransformRequested>>();

    // Style is excluded from undo AND TransactionManager's aux-diff
    // (view-setting state, not document content), but still dirty-tracked
    // below since style IS saved in the .plr.
    kernel.registerCommand<plnr::events::SetFaceStyleRequested, plnr::agent::SetFaceStyleCommand>();
    kernel.registerCommand<plnr::events::SetEdgeStyleFlagRequested, plnr::agent::SetEdgeStyleFlagCommand>();
    kernel.registerCommand<plnr::events::SetAmbientOcclusionRequested, plnr::agent::SetAmbientOcclusionCommand>();
    kernel.registerCommand<plnr::events::SetAoStrengthRequested, plnr::agent::SetAoStrengthCommand>();

    // Same excluded-from-undo/aux-diff-but-dirty-tracked treatment as Style above.
    kernel.registerCommand<plnr::events::SetUseSunForShadingRequested, plnr::agent::SetUseSunForShadingCommand>();
    kernel.registerCommand<plnr::events::SetShowShadowsRequested, plnr::agent::SetShowShadowsCommand>();
    kernel.registerCommand<plnr::events::SetSunPositionRequested, plnr::agent::SetSunPositionCommand>();
    kernel.registerCommand<plnr::events::SetSunDateTimeRequested, plnr::agent::SetSunDateTimeCommand>();
    kernel.registerCommand<plnr::events::SetShadowLightRequested, plnr::agent::SetShadowLightCommand>();
    kernel.registerCommand<plnr::events::SetShadowDarkRequested, plnr::agent::SetShadowDarkCommand>();

    // Same treatment as Shadows above.
    kernel.registerCommand<plnr::events::SetFogEnabledRequested, plnr::agent::SetFogEnabledCommand>();
    kernel.registerCommand<plnr::events::SetFogRangeRequested, plnr::agent::SetFogRangeCommand>();
    kernel.registerCommand<plnr::events::SetFogUseBackgroundColorRequested,
                            plnr::agent::SetFogUseBackgroundColorCommand>();

    // Camera sync + document New/Open/Save.
    kernel.registerCommand<plnr::events::CameraNavigated, plnr::agent::CameraSyncCommand>();
    kernel.registerCommand<plnr::events::SetProjectionRequested, plnr::agent::SetProjectionCommand>();
    kernel.registerCommand<plnr::events::SetStandardViewRequested, plnr::agent::SetStandardViewCommand>();
    // Session-only view history, never undone.
    kernel.registerCommand<plnr::events::CameraPreviousRequested, plnr::agent::CameraPreviousCommand>();
    kernel.registerCommand<plnr::events::CameraNextRequested, plnr::agent::CameraNextCommand>();
    kernel.registerCommand<plnr::events::NewDocumentRequested, plnr::agent::NewDocumentCommand>();
    kernel.registerCommand<plnr::events::OpenDocumentRequested, plnr::agent::OpenDocumentCommand>();
    kernel.registerCommand<plnr::events::SaveDocumentRequested, plnr::agent::SaveDocumentCommand>();

    // ExportObjRequested is read-only (no dirty/undo). ImportObjRequested
    // builds a new Definition+Instance -- wrapped like other geometry-creating Intents.
    kernel.registerCommand<plnr::events::ExportObjRequested, plnr::agent::ExportObjCommand>();
    kernel.registerCommand<
        plnr::events::ImportObjRequested,
        plnr::agent::UndoCaptureCommand<plnr::agent::ImportObjCommand, plnr::events::ImportObjRequested>>();

    // Never wrapped themselves (would create their own undo steps).
    kernel.registerCommand<plnr::events::UndoRequested, plnr::agent::UndoCommand>();
    kernel.registerCommand<plnr::events::RedoRequested, plnr::agent::RedoCommand>();

    // GeometryChanged's dirty-marking is already inside GeometryChangedCommand
    // above (Ordo's registerCommand is single-slot per event type --
    // registering it again here would silently replace that reaction). Facts
    // below get a plain MarkDirtyCommand<EventT>; Selection/EditContext/CameraChanged never mark dirty.
    kernel.registerCommand<plnr::events::TagsChanged, plnr::agent::MarkDirtyCommand<plnr::events::TagsChanged>>();
    kernel.registerCommand<plnr::events::GuidesChanged, plnr::agent::MarkDirtyCommand<plnr::events::GuidesChanged>>();
    kernel.registerCommand<plnr::events::AnnotationsChanged,
                            plnr::agent::MarkDirtyCommand<plnr::events::AnnotationsChanged>>();
    kernel.registerCommand<plnr::events::SectionsChanged, plnr::agent::MarkDirtyCommand<plnr::events::SectionsChanged>>();
    kernel.registerCommand<plnr::events::AxesChanged, plnr::agent::MarkDirtyCommand<plnr::events::AxesChanged>>();
    kernel.registerCommand<plnr::events::MaterialsChanged,
                            plnr::agent::MarkDirtyCommand<plnr::events::MaterialsChanged>>();
    kernel.registerCommand<plnr::events::StyleChanged, plnr::agent::MarkDirtyCommand<plnr::events::StyleChanged>>();
    kernel.registerCommand<plnr::events::ShadowsChanged, plnr::agent::MarkDirtyCommand<plnr::events::ShadowsChanged>>();
    kernel.registerCommand<plnr::events::FogChanged, plnr::agent::MarkDirtyCommand<plnr::events::FogChanged>>();

    kernel.registerCommand<
        plnr::events::SolidOpRequested,
        plnr::agent::UndoCaptureCommand<plnr::agent::SolidOpCommand, plnr::events::SolidOpRequested>>();

    // Constructed after commands are registered (so injected input can
    // round-trip immediately) and before show().
    const quint16 debugBridgePort = debugBridgePortFromArgs(argc, argv);
    std::unique_ptr<plnr::devbridge::DebugBridge> debugBridge;
    if (debugBridgePort != 0) {
        debugBridge = std::make_unique<plnr::devbridge::DebugBridge>(kernel, &window, debugBridgePort);
    }

    // --start-minimized: agent-driven test runs; neither bridge injection
    // nor grabFramebuffer() needs the window visible.
    if (hasFlag(argc, argv, "--start-minimized")) {
        window.showMinimized();
    } else {
        window.show();
    }

    const bool demoGeometry = hasFlag(argc, argv, "--demo-geometry");
    const bool demoExtrude = hasFlag(argc, argv, "--demo-extrude");
    if (demoGeometry || demoExtrude) {
        // 4x3 rectangle on the ground plane; the closing edge completes a face.
        kernel.send(plnr::events::AddEdgeRequested{{0.0, 0.0, 0.0}, {4.0, 0.0, 0.0}});
        kernel.send(plnr::events::AddEdgeRequested{{4.0, 0.0, 0.0}, {4.0, 3.0, 0.0}});
        kernel.send(plnr::events::AddEdgeRequested{{4.0, 3.0, 0.0}, {0.0, 3.0, 0.0}});
        kernel.send(plnr::events::AddEdgeRequested{{0.0, 3.0, 0.0}, {0.0, 0.0, 0.0}});
    }

    if (demoExtrude) {
        // Push the demo rectangle's face into a box (ExtrudeFaceRequested).
        auto agent = kernel.agentAs<plnr::agent::GeometryApi>(plnr::agent::kGeometryApiName);
        if (agent && !agent->model().faces().empty()) {
            const plnr::geo::Id faceId = agent->model().faces().begin()->first;
            kernel.send(plnr::events::ExtrudeFaceRequested{faceId, 2.0});
        }
    }

    const QString screenshotPath = screenshotPathFromArgs(argc, argv);
    if (!screenshotPath.isEmpty()) {
        QTimer::singleShot(1500, &window, [&window, screenshotPath]() {
            window.viewportWidget()->grabFramebuffer().save(screenshotPath);
            QApplication::quit();
        });
    }

    return app.exec();
}
