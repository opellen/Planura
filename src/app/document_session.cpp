#include "document_session.h"

#include <memory>

#include <QString>

#include "agent/annotation_store.h"
#include "agent/command/annotation_commands.h"
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
#include "tools/tool_controller.h"
#include "ui/tab_badge_adapter.h"
#include "viewport/viewport_widget.h"

namespace plnr {

DocumentSession::DocumentSession() {
    // Bootstrap order (policy §4): construct kernel -> register Agents ->
    // construct Presenters (inside MainWindow) -> registerCommands -> show.
    kernel_.registerAgent(std::make_shared<plnr::agent::GeometryApi>());
    kernel_.registerAgent(std::make_shared<plnr::agent::SelectionStore>());
    kernel_.registerAgent(std::make_shared<plnr::agent::TagStore>());
    kernel_.registerAgent(std::make_shared<plnr::agent::EditContextStore>());
    kernel_.registerAgent(std::make_shared<plnr::agent::GuideStore>());
    kernel_.registerAgent(std::make_shared<plnr::agent::AxesStore>());
    kernel_.registerAgent(std::make_shared<plnr::agent::AnnotationStore>());
    kernel_.registerAgent(std::make_shared<plnr::agent::SectionStore>());
    // MaterialRepository: in-model material list + per-entity paint assignments.
    kernel_.registerAgent(std::make_shared<plnr::agent::MaterialRepository>());
    // AssetRepository/StyleStore/ShadowStore/FogStore must register before
    // document_commands.cpp's New/Open/Save (DocumentStores::ok() requires it).
    kernel_.registerAgent(std::make_shared<plnr::agent::AssetRepository>());
    kernel_.registerAgent(std::make_shared<plnr::agent::StyleStore>());
    kernel_.registerAgent(std::make_shared<plnr::agent::ShadowStore>());
    kernel_.registerAgent(std::make_shared<plnr::agent::FogStore>());
    kernel_.registerAgent(std::make_shared<plnr::agent::DocumentStore>());
    kernel_.registerAgent(std::make_shared<plnr::agent::CameraStore>());
    kernel_.registerAgent(std::make_shared<plnr::agent::UndoStore>());

    // After the Agents: the adapter's initial snapshot reads DocumentStore.
    badgeHost_ = std::make_unique<ordo::qt::ViewHost>(kernel_);
    badge_ = badgeHost_->add<ui::TabBadgeAdapter>();
}

DocumentSession::~DocumentSession() = default;

void DocumentSession::createViewObjects() {
    viewport_ = new viewport::ViewportWidget(nullptr);
    viewport_->setObjectName(QStringLiteral("viewport"));
}

void DocumentSession::recreateViewObjects() {
    resetToolHost();
    viewport_ = nullptr;
    createViewObjects();
}

void DocumentSession::createToolHost() {
    if (sessionHost_) return;
    sessionHost_ = std::make_unique<ordo::qt::ViewHost>(kernel_);
    toolController_ = sessionHost_->add<tools::ToolController>(kernel_, viewport_);
}

void DocumentSession::resetToolHost() {
    toolController_ = nullptr;
    sessionHost_.reset();
}

void DocumentSession::registerCommands() {
    // Every mutating Intent is wrapped in UndoCaptureCommand<RealCommand, EventT>, or it
    // becomes silently un-undoable. Excluded: view-state-only events (Select*,
    // EnterContext/ExitContext, camera/projection, transient UI state) and *Changed reactions.
    kernel_.registerCommand<plnr::events::AddEdgeRequested,
                            plnr::agent::UndoCaptureCommand<plnr::agent::AddEdgeCommand, plnr::events::AddEdgeRequested>>();
    kernel_.registerCommand<
        plnr::events::AddRectangleRequested,
        plnr::agent::UndoCaptureCommand<plnr::agent::AddRectangleCommand, plnr::events::AddRectangleRequested>>();
    kernel_.registerCommand<
        plnr::events::ExtrudeFaceRequested,
        plnr::agent::UndoCaptureCommand<plnr::agent::ExtrudeFaceCommand, plnr::events::ExtrudeFaceRequested>>();
    kernel_.registerCommand<
        plnr::events::MoveEntityRequested,
        plnr::agent::UndoCaptureCommand<plnr::agent::MoveEntityCommand, plnr::events::MoveEntityRequested>>();
    kernel_.registerCommand<
        plnr::events::RemoveEdgeRequested,
        plnr::agent::UndoCaptureCommand<plnr::agent::RemoveEdgeCommand, plnr::events::RemoveEdgeRequested>>();
    kernel_.registerCommand<
        plnr::events::AddPolylineRequested,
        plnr::agent::UndoCaptureCommand<plnr::agent::AddPolylineCommand, plnr::events::AddPolylineRequested>>();
    kernel_.registerCommand<
        plnr::events::Add3dTextRequested,
        plnr::agent::UndoCaptureCommand<plnr::agent::Add3dTextCommand, plnr::events::Add3dTextRequested>>();
    kernel_.registerCommand<plnr::events::ReplaceLastPolylineRequested,
                            plnr::agent::UndoCaptureCommand<plnr::agent::ReplaceLastPolylineCommand,
                                                             plnr::events::ReplaceLastPolylineRequested>>();
    kernel_.registerCommand<plnr::events::TransformEntitiesRequested,
                            plnr::agent::UndoCaptureCommand<plnr::agent::TransformEntitiesCommand,
                                                             plnr::events::TransformEntitiesRequested>>();
    kernel_.registerCommand<
        plnr::events::ApplyArrayRequested,
        plnr::agent::UndoCaptureCommand<plnr::agent::ApplyArrayCommand, plnr::events::ApplyArrayRequested>>();
    kernel_.registerCommand<plnr::events::FollowMeRequested,
                            plnr::agent::UndoCaptureCommand<plnr::agent::FollowMeCommand, plnr::events::FollowMeRequested>>();
    kernel_.registerCommand<
        plnr::events::DivideEdgeRequested,
        plnr::agent::UndoCaptureCommand<plnr::agent::DivideEdgeCommand, plnr::events::DivideEdgeRequested>>();
    // Combines PruneSelectionCommand's reaction + AnnotationStore dimension
    // refresh (Ordo allows only one registered Command per event type).
    kernel_.registerCommand<plnr::events::GeometryChanged, plnr::agent::GeometryChangedCommand>();
    // Select None is a default-constructed SelectRequested{}, already covered here.
    kernel_.registerCommand<plnr::events::SelectRequested, plnr::agent::SelectCommand>();
    kernel_.registerCommand<plnr::events::SelectRegionRequested, plnr::agent::SelectRegionCommand>();
    kernel_.registerCommand<plnr::events::SelectAllRequested, plnr::agent::SelectAllCommand>();
    kernel_.registerCommand<
        plnr::events::SetHiddenRequested,
        plnr::agent::UndoCaptureCommand<plnr::agent::SetHiddenCommand, plnr::events::SetHiddenRequested>>();
    kernel_.registerCommand<
        plnr::events::UnhideAllRequested,
        plnr::agent::UndoCaptureCommand<plnr::agent::UnhideAllCommand, plnr::events::UnhideAllRequested>>();
    kernel_.registerCommand<
        plnr::events::DeleteSelectionRequested,
        plnr::agent::UndoCaptureCommand<plnr::agent::DeleteSelectionCommand, plnr::events::DeleteSelectionRequested>>();
    kernel_.registerCommand<
        plnr::events::GroupCreateRequested,
        plnr::agent::UndoCaptureCommand<plnr::agent::GroupCreateCommand, plnr::events::GroupCreateRequested>>();
    kernel_.registerCommand<plnr::events::ExplodeRequested,
                            plnr::agent::UndoCaptureCommand<plnr::agent::ExplodeCommand, plnr::events::ExplodeRequested>>();
    kernel_.registerCommand<plnr::events::EnterContextRequested, plnr::agent::EnterContextCommand>();
    kernel_.registerCommand<plnr::events::ExitContextRequested, plnr::agent::ExitContextCommand>();
    kernel_.registerCommand<
        plnr::events::TagCreateRequested,
        plnr::agent::UndoCaptureCommand<plnr::agent::TagCreateCommand, plnr::events::TagCreateRequested>>();
    kernel_.registerCommand<
        plnr::events::TagAssignRequested,
        plnr::agent::UndoCaptureCommand<plnr::agent::TagAssignCommand, plnr::events::TagAssignRequested>>();
    kernel_.registerCommand<
        plnr::events::TagVisibilityRequested,
        plnr::agent::UndoCaptureCommand<plnr::agent::TagVisibilityCommand, plnr::events::TagVisibilityRequested>>();
    kernel_.registerCommand<
        plnr::events::AddGuideLineRequested,
        plnr::agent::UndoCaptureCommand<plnr::agent::AddGuideLineCommand, plnr::events::AddGuideLineRequested>>();
    kernel_.registerCommand<
        plnr::events::AddGuidePointRequested,
        plnr::agent::UndoCaptureCommand<plnr::agent::AddGuidePointCommand, plnr::events::AddGuidePointRequested>>();
    kernel_.registerCommand<
        plnr::events::EraseGuideRequested,
        plnr::agent::UndoCaptureCommand<plnr::agent::EraseGuideCommand, plnr::events::EraseGuideRequested>>();
    kernel_.registerCommand<plnr::events::DeleteAllGuidesRequested,
                            plnr::agent::UndoCaptureCommand<plnr::agent::DeleteAllGuidesCommand,
                                                             plnr::events::DeleteAllGuidesRequested>>();
    kernel_.registerCommand<
        plnr::events::SetGuideHiddenRequested,
        plnr::agent::UndoCaptureCommand<plnr::agent::SetGuideHiddenCommand, plnr::events::SetGuideHiddenRequested>>();
    kernel_.registerCommand<plnr::events::SetAxesRequested,
                            plnr::agent::UndoCaptureCommand<plnr::agent::SetAxesCommand, plnr::events::SetAxesRequested>>();
    kernel_.registerCommand<
        plnr::events::ResetAxesRequested,
        plnr::agent::UndoCaptureCommand<plnr::agent::ResetAxesCommand, plnr::events::ResetAxesRequested>>();
    kernel_.registerCommand<
        plnr::events::AddDimensionRequested,
        plnr::agent::UndoCaptureCommand<plnr::agent::AddDimensionCommand, plnr::events::AddDimensionRequested>>();
    kernel_.registerCommand<
        plnr::events::AddScreenTextRequested,
        plnr::agent::UndoCaptureCommand<plnr::agent::AddScreenTextCommand, plnr::events::AddScreenTextRequested>>();
    kernel_.registerCommand<
        plnr::events::AddLeaderTextRequested,
        plnr::agent::UndoCaptureCommand<plnr::agent::AddLeaderTextCommand, plnr::events::AddLeaderTextRequested>>();
    kernel_.registerCommand<plnr::events::SetAnnotationTextRequested,
                            plnr::agent::UndoCaptureCommand<plnr::agent::SetAnnotationTextCommand,
                                                             plnr::events::SetAnnotationTextRequested>>();
    kernel_.registerCommand<
        plnr::events::RemoveAnnotationRequested,
        plnr::agent::UndoCaptureCommand<plnr::agent::RemoveAnnotationCommand, plnr::events::RemoveAnnotationRequested>>();
    kernel_.registerCommand<plnr::events::RemoveAllAnnotationsRequested,
                            plnr::agent::UndoCaptureCommand<plnr::agent::RemoveAllAnnotationsCommand,
                                                             plnr::events::RemoveAllAnnotationsRequested>>();
    kernel_.registerCommand<
        plnr::events::AddSectionPlaneRequested,
        plnr::agent::UndoCaptureCommand<plnr::agent::AddSectionPlaneCommand, plnr::events::AddSectionPlaneRequested>>();
    kernel_.registerCommand<plnr::events::RemoveSectionPlaneRequested,
                            plnr::agent::UndoCaptureCommand<plnr::agent::RemoveSectionPlaneCommand,
                                                             plnr::events::RemoveSectionPlaneRequested>>();
    kernel_.registerCommand<plnr::events::SetSectionActiveRequested,
                            plnr::agent::UndoCaptureCommand<plnr::agent::SetSectionActiveCommand,
                                                             plnr::events::SetSectionActiveRequested>>();
    kernel_.registerCommand<
        plnr::events::ReverseSectionRequested,
        plnr::agent::UndoCaptureCommand<plnr::agent::ReverseSectionCommand, plnr::events::ReverseSectionRequested>>();
    kernel_.registerCommand<plnr::events::SetSectionHiddenRequested,
                            plnr::agent::UndoCaptureCommand<plnr::agent::SetSectionHiddenCommand,
                                                             plnr::events::SetSectionHiddenRequested>>();

    kernel_.registerCommand<
        plnr::events::MaterialCreateRequested,
        plnr::agent::UndoCaptureCommand<plnr::agent::MaterialCreateCommand, plnr::events::MaterialCreateRequested>>();
    kernel_.registerCommand<
        plnr::events::MaterialEditRequested,
        plnr::agent::UndoCaptureCommand<plnr::agent::MaterialEditCommand, plnr::events::MaterialEditRequested>>();
    kernel_.registerCommand<plnr::events::PaintRequested,
                            plnr::agent::UndoCaptureCommand<plnr::agent::PaintCommand, plnr::events::PaintRequested>>();
    // SetActiveMaterialRequested is transient UI state (not document content) -- not wrapped.
    kernel_.registerCommand<plnr::events::SetActiveMaterialRequested, plnr::agent::SetActiveMaterialCommand>();

    // Undo reverts only the Material's {assetHash, tileW, tileH} fields;
    // the AssetRepository blob itself is never undo-captured.
    kernel_.registerCommand<plnr::events::MaterialSetTextureRequested,
                            plnr::agent::UndoCaptureCommand<plnr::agent::MaterialSetTextureCommand,
                                                             plnr::events::MaterialSetTextureRequested>>();

    // Undo reverts the whole {materials, assignments} aux-diff snapshot,
    // which already carries uvTransform.
    kernel_.registerCommand<
        plnr::events::SetUvTransformRequested,
        plnr::agent::UndoCaptureCommand<plnr::agent::SetUvTransformCommand, plnr::events::SetUvTransformRequested>>();

    // Style is excluded from undo AND Transaction's aux-diff
    // (view-setting state, not document content), but still dirty-tracked
    // below since style IS saved in the .plr.
    kernel_.registerCommand<plnr::events::SetFaceStyleRequested, plnr::agent::SetFaceStyleCommand>();
    kernel_.registerCommand<plnr::events::SetEdgeStyleFlagRequested, plnr::agent::SetEdgeStyleFlagCommand>();
    kernel_.registerCommand<plnr::events::SetAmbientOcclusionRequested, plnr::agent::SetAmbientOcclusionCommand>();
    kernel_.registerCommand<plnr::events::SetAoStrengthRequested, plnr::agent::SetAoStrengthCommand>();

    // Same excluded-from-undo/aux-diff-but-dirty-tracked treatment as Style above.
    kernel_.registerCommand<plnr::events::SetUseSunForShadingRequested, plnr::agent::SetUseSunForShadingCommand>();
    kernel_.registerCommand<plnr::events::SetShowShadowsRequested, plnr::agent::SetShowShadowsCommand>();
    kernel_.registerCommand<plnr::events::SetSunPositionRequested, plnr::agent::SetSunPositionCommand>();
    kernel_.registerCommand<plnr::events::SetSunDateTimeRequested, plnr::agent::SetSunDateTimeCommand>();
    kernel_.registerCommand<plnr::events::SetShadowLightRequested, plnr::agent::SetShadowLightCommand>();
    kernel_.registerCommand<plnr::events::SetShadowDarkRequested, plnr::agent::SetShadowDarkCommand>();

    // Same treatment as Shadows above.
    kernel_.registerCommand<plnr::events::SetFogEnabledRequested, plnr::agent::SetFogEnabledCommand>();
    kernel_.registerCommand<plnr::events::SetFogRangeRequested, plnr::agent::SetFogRangeCommand>();
    kernel_.registerCommand<plnr::events::SetFogUseBackgroundColorRequested,
                            plnr::agent::SetFogUseBackgroundColorCommand>();

    // Camera sync + document New/Open/Save.
    kernel_.registerCommand<plnr::events::CameraNavigated, plnr::agent::CameraSyncCommand>();
    kernel_.registerCommand<plnr::events::SetProjectionRequested, plnr::agent::SetProjectionCommand>();
    kernel_.registerCommand<plnr::events::SetStandardViewRequested, plnr::agent::SetStandardViewCommand>();
    // Session-only view history, never undone.
    kernel_.registerCommand<plnr::events::CameraPreviousRequested, plnr::agent::CameraPreviousCommand>();
    kernel_.registerCommand<plnr::events::CameraNextRequested, plnr::agent::CameraNextCommand>();
    kernel_.registerCommand<plnr::events::NewDocumentRequested, plnr::agent::NewDocumentCommand>();
    kernel_.registerCommand<plnr::events::OpenDocumentRequested, plnr::agent::OpenDocumentCommand>();
    kernel_.registerCommand<plnr::events::SaveDocumentRequested, plnr::agent::SaveDocumentCommand>();
    // Async UI route; the sync commands above stay for the bridge.
    kernel_.registerCommand<plnr::events::OpenDocumentDataReady, plnr::agent::OpenDocumentDataCommand>();
    kernel_.registerCommand<plnr::events::SaveSnapshotRequested, plnr::agent::SaveSnapshotCommand>();
    kernel_.registerCommand<plnr::events::SaveCommitted, plnr::agent::SaveCommittedCommand>();

    // ExportObjRequested is read-only (no dirty/undo). ImportObjRequested
    // builds a new Definition+Instance -- wrapped like other geometry-creating Intents.
    kernel_.registerCommand<plnr::events::ExportObjRequested, plnr::agent::ExportObjCommand>();
    kernel_.registerCommand<
        plnr::events::ImportObjRequested,
        plnr::agent::UndoCaptureCommand<plnr::agent::ImportObjCommand, plnr::events::ImportObjRequested>>();
    // Async UI route; the sync commands above stay for the bridge.
    kernel_.registerCommand<plnr::events::ExportObjSnapshotRequested, plnr::agent::ExportObjSnapshotCommand>();
    kernel_.registerCommand<
        plnr::events::ImportObjDataReady,
        plnr::agent::UndoCaptureCommand<plnr::agent::ImportObjDataCommand, plnr::events::ImportObjDataReady>>();

    // Never wrapped themselves (would create their own undo steps).
    kernel_.registerCommand<plnr::events::UndoRequested, plnr::agent::UndoCommand>();
    kernel_.registerCommand<plnr::events::RedoRequested, plnr::agent::RedoCommand>();

    // GeometryChanged's dirty-marking is already in GeometryChangedCommand above
    // (registerCommand is single-slot per event type; re-registering would replace it).
    // Facts below get a plain MarkDirtyCommand<EventT>; Selection/EditContext/CameraChanged never mark dirty.
    kernel_.registerCommand<plnr::events::TagsChanged, plnr::agent::MarkDirtyCommand<plnr::events::TagsChanged>>();
    kernel_.registerCommand<plnr::events::GuidesChanged, plnr::agent::MarkDirtyCommand<plnr::events::GuidesChanged>>();
    kernel_.registerCommand<plnr::events::AnnotationsChanged,
                            plnr::agent::MarkDirtyCommand<plnr::events::AnnotationsChanged>>();
    kernel_.registerCommand<plnr::events::SectionsChanged, plnr::agent::MarkDirtyCommand<plnr::events::SectionsChanged>>();
    kernel_.registerCommand<plnr::events::AxesChanged, plnr::agent::MarkDirtyCommand<plnr::events::AxesChanged>>();
    kernel_.registerCommand<plnr::events::MaterialsChanged,
                            plnr::agent::MarkDirtyCommand<plnr::events::MaterialsChanged>>();
    kernel_.registerCommand<plnr::events::StyleChanged, plnr::agent::MarkDirtyCommand<plnr::events::StyleChanged>>();
    kernel_.registerCommand<plnr::events::ShadowsChanged, plnr::agent::MarkDirtyCommand<plnr::events::ShadowsChanged>>();
    kernel_.registerCommand<plnr::events::FogChanged, plnr::agent::MarkDirtyCommand<plnr::events::FogChanged>>();

    kernel_.registerCommand<
        plnr::events::SolidOpRequested,
        plnr::agent::UndoCaptureCommand<plnr::agent::SolidOpCommand, plnr::events::SolidOpRequested>>();
}

}  // namespace plnr
