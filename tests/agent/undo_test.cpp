#include "agent/command/undo_commands.h"

#include <memory>
#include <string>
#include <vector>

#include <QByteArray>
#include <QDir>
#include <QJsonDocument>
#include <QString>
#include <QTemporaryDir>

#include <ordo/core/app_kernel.h>

#include "agent/command/annotation_commands.h"
#include "agent/annotation_store.h"
#include "agent/asset_repository.h"
#include "agent/command/axes_commands.h"
#include "agent/axes_store.h"
#include "agent/camera_store.h"
#include "agent/command/document_commands.h"
#include "agent/document_store.h"
#include "agent/edit_context_store.h"
#include "agent/events.h"
#include "agent/command/fog_commands.h"
#include "agent/fog_store.h"
#include "agent/command/geometry_commands.h"
#include "agent/geometry_api.h"
#include "agent/command/group_commands.h"
#include "agent/command/guide_commands.h"
#include "agent/guide_store.h"
#include "agent/material_repository.h"
#include "agent/command/section_commands.h"
#include "agent/section_store.h"
#include "agent/command/selection_commands.h"
#include "agent/selection_store.h"
#include "agent/command/shadow_commands.h"
#include "agent/shadow_store.h"
#include "agent/command/style_commands.h"
#include "agent/style_store.h"
#include "agent/command/tag_commands.h"
#include "agent/tag_store.h"
#include "agent/transaction_manager.h"
#include "agent/undo_store.h"
#include "io/plr_writer.h"

#include <geo/entity.h>
#include <geo/scene.h>
#include <geo/vec3.h>

#include <gtest/gtest.h>

namespace {

using ordo::core::AppKernel;
using plnr::agent::AddDimensionCommand;
using plnr::agent::AddEdgeCommand;
using plnr::agent::AddGuidePointCommand;
using plnr::agent::AddRectangleCommand;
using plnr::agent::AddSectionPlaneCommand;
using plnr::agent::AnnotationStore;
using plnr::agent::AssetRepository;
using plnr::agent::AxesStore;
using plnr::agent::CameraState;
using plnr::agent::CameraStore;
using plnr::agent::CameraSyncCommand;
using plnr::agent::Dimension;
using plnr::agent::DocumentStore;
using plnr::agent::EditContextStore;
using plnr::agent::ExplodeCommand;
using plnr::agent::FogStore;
using plnr::agent::GeometryChangedCommand;
using plnr::agent::GeometryApi;
using plnr::agent::GroupCreateCommand;
using plnr::agent::Guide;
using plnr::agent::GuideStore;
using plnr::agent::kAnnotationStoreName;
using plnr::agent::kAxesStoreName;
using plnr::agent::kCameraStoreName;
using plnr::agent::kDocumentStoreName;
using plnr::agent::kEditContextStoreName;
using plnr::agent::kFogStoreName;
using plnr::agent::kGeometryApiName;
using plnr::agent::kGuideStoreName;
using plnr::agent::kMaterialRepositoryName;
using plnr::agent::kSectionStoreName;
using plnr::agent::kSelectionStoreName;
using plnr::agent::kShadowStoreName;
using plnr::agent::kStyleStoreName;
using plnr::agent::kTagStoreName;
using plnr::agent::kUndoStoreName;
using plnr::agent::MarkDirtyCommand;
using plnr::agent::MaterialRepository;
using plnr::agent::MoveEntityCommand;
using plnr::agent::NewDocumentCommand;
using plnr::agent::OpenDocumentCommand;
using plnr::agent::RedoCommand;
using plnr::agent::SaveDocumentCommand;
using plnr::agent::SectionStore;
using plnr::agent::SelectCommand;
using plnr::agent::SelectionStore;
using plnr::agent::SetEdgeStyleFlagCommand;
using plnr::agent::SetFaceStyleCommand;
using plnr::agent::SetFogEnabledCommand;
using plnr::agent::SetFogRangeCommand;
using plnr::agent::SetFogUseBackgroundColorCommand;
using plnr::agent::SetHiddenCommand;
using plnr::agent::SetShowShadowsCommand;
using plnr::agent::SetSunDateTimeCommand;
using plnr::agent::SetSunPositionCommand;
using plnr::agent::SetUseSunForShadingCommand;
using plnr::agent::ShadowStore;
using plnr::agent::StyleStore;
using plnr::agent::TagCreateCommand;
using plnr::agent::TagAssignCommand;
using plnr::agent::TagStore;
using plnr::agent::TransactionManager;
using plnr::agent::UndoCaptureCommand;
using plnr::agent::UndoCommand;
using plnr::agent::UndoStore;
using plnr::events::AddDimensionRequested;
using plnr::events::AddEdgeRequested;
using plnr::events::AddGuidePointRequested;
using plnr::events::AddRectangleRequested;
using plnr::events::AddSectionPlaneRequested;
using plnr::events::AnnotationsChanged;
using plnr::events::AxesChanged;
using plnr::events::CameraNavigated;
using plnr::events::EntityRef;
using plnr::events::ExplodeRequested;
using plnr::events::FogChanged;
using plnr::events::GeometryChanged;
using plnr::events::GroupCreateRequested;
using plnr::events::GuidesChanged;
using plnr::events::MoveEntityRequested;
using plnr::events::NewDocumentRequested;
using plnr::events::OpenDocumentRequested;
using plnr::events::SaveDocumentRequested;
using plnr::events::SectionsChanged;
using plnr::events::SelectExpand;
using plnr::events::SelectMode;
using plnr::events::SelectRequested;
using plnr::events::EdgeFlag;
using plnr::events::FaceStyle;
using plnr::events::SetAxesRequested;
using plnr::events::SetEdgeStyleFlagRequested;
using plnr::events::SetFaceStyleRequested;
using plnr::events::SetFogEnabledRequested;
using plnr::events::SetFogRangeRequested;
using plnr::events::SetFogUseBackgroundColorRequested;
using plnr::events::SetHiddenRequested;
using plnr::events::SetShowShadowsRequested;
using plnr::events::SetSunDateTimeRequested;
using plnr::events::SetSunPositionRequested;
using plnr::events::SetUseSunForShadingRequested;
using plnr::events::ShadowsChanged;
using plnr::events::StyleChanged;
using plnr::events::TagAssignRequested;
using plnr::events::TagCreateRequested;
using plnr::events::TagsChanged;
using plnr::events::UndoRequested;
using plnr::events::UndoStateChanged;
using plnr::events::RedoRequested;
using plnr::geo::EntityKind;
using plnr::geo::Id;
using plnr::geo::kRootDefinitionId;

// -- Shared fixture: a full kernel with every mutating Intent wrapped in
// UndoCaptureCommand (a subset of main.cpp's own registration list). ------
class UndoTest : public ::testing::Test {
protected:
    void SetUp() override {
        kernel.registerAgent(std::make_shared<GeometryApi>());
        kernel.registerAgent(std::make_shared<TagStore>());
        kernel.registerAgent(std::make_shared<GuideStore>());
        kernel.registerAgent(std::make_shared<AnnotationStore>());
        kernel.registerAgent(std::make_shared<SectionStore>());
        kernel.registerAgent(std::make_shared<AxesStore>());
        kernel.registerAgent(std::make_shared<MaterialRepository>());
        kernel.registerAgent(std::make_shared<AssetRepository>());
        kernel.registerAgent(std::make_shared<StyleStore>());
        kernel.registerAgent(std::make_shared<ShadowStore>());
        kernel.registerAgent(std::make_shared<FogStore>());
        kernel.registerAgent(std::make_shared<SelectionStore>());
        kernel.registerAgent(std::make_shared<EditContextStore>());
        kernel.registerAgent(std::make_shared<CameraStore>());
        kernel.registerAgent(std::make_shared<DocumentStore>());
        kernel.registerAgent(std::make_shared<UndoStore>());

        // Mutating Intents -- wrapped, same discipline as main.cpp.
        kernel.registerCommand<AddEdgeRequested, UndoCaptureCommand<AddEdgeCommand, AddEdgeRequested>>();
        kernel.registerCommand<AddRectangleRequested, UndoCaptureCommand<AddRectangleCommand, AddRectangleRequested>>();
        kernel.registerCommand<MoveEntityRequested, UndoCaptureCommand<MoveEntityCommand, MoveEntityRequested>>();
        kernel.registerCommand<TagCreateRequested, UndoCaptureCommand<TagCreateCommand, TagCreateRequested>>();
        kernel.registerCommand<TagAssignRequested, UndoCaptureCommand<TagAssignCommand, TagAssignRequested>>();
        kernel.registerCommand<AddGuidePointRequested,
                                UndoCaptureCommand<AddGuidePointCommand, AddGuidePointRequested>>();
        kernel.registerCommand<SetAxesRequested, UndoCaptureCommand<plnr::agent::SetAxesCommand, SetAxesRequested>>();
        kernel.registerCommand<AddSectionPlaneRequested,
                                UndoCaptureCommand<AddSectionPlaneCommand, AddSectionPlaneRequested>>();
        kernel.registerCommand<AddDimensionRequested, UndoCaptureCommand<AddDimensionCommand, AddDimensionRequested>>();
        kernel.registerCommand<SetHiddenRequested, UndoCaptureCommand<SetHiddenCommand, SetHiddenRequested>>();
        kernel.registerCommand<GroupCreateRequested, UndoCaptureCommand<GroupCreateCommand, GroupCreateRequested>>();
        kernel.registerCommand<ExplodeRequested, UndoCaptureCommand<ExplodeCommand, ExplodeRequested>>();

        // *Changed reactions (dirty tracking + the aux-touch ping), and
        // Select -- view-state, never wrapped, same as main.cpp.
        kernel.registerCommand<GeometryChanged, GeometryChangedCommand>();
        kernel.registerCommand<TagsChanged, MarkDirtyCommand<TagsChanged>>();
        kernel.registerCommand<GuidesChanged, MarkDirtyCommand<GuidesChanged>>();
        kernel.registerCommand<AnnotationsChanged, MarkDirtyCommand<AnnotationsChanged>>();
        kernel.registerCommand<SectionsChanged, MarkDirtyCommand<SectionsChanged>>();
        kernel.registerCommand<AxesChanged, MarkDirtyCommand<AxesChanged>>();
        kernel.registerCommand<plnr::events::MaterialsChanged, MarkDirtyCommand<plnr::events::MaterialsChanged>>();
        // style Requested events are deliberately NOT wrapped in
        // UndoCaptureCommand (view-setting semantics) -- StyleChanged IS a
        // dirty-tracked fact though.
        kernel.registerCommand<SetFaceStyleRequested, SetFaceStyleCommand>();
        kernel.registerCommand<SetEdgeStyleFlagRequested, SetEdgeStyleFlagCommand>();
        kernel.registerCommand<StyleChanged, MarkDirtyCommand<StyleChanged>>();
        // shadow Requested events are deliberately NOT wrapped in
        // UndoCaptureCommand (view-setting semantics) -- ShadowsChanged IS a
        // dirty-tracked fact though.
        kernel.registerCommand<SetUseSunForShadingRequested, SetUseSunForShadingCommand>();
        kernel.registerCommand<SetShowShadowsRequested, SetShowShadowsCommand>();
        kernel.registerCommand<SetSunPositionRequested, SetSunPositionCommand>();
        kernel.registerCommand<SetSunDateTimeRequested, SetSunDateTimeCommand>();
        kernel.registerCommand<ShadowsChanged, MarkDirtyCommand<ShadowsChanged>>();
        // fog Requested events -- same "view-setting, not undo-wrapped" rule
        // as the shadow block above.
        kernel.registerCommand<SetFogEnabledRequested, SetFogEnabledCommand>();
        kernel.registerCommand<SetFogRangeRequested, SetFogRangeCommand>();
        kernel.registerCommand<SetFogUseBackgroundColorRequested, SetFogUseBackgroundColorCommand>();
        kernel.registerCommand<FogChanged, MarkDirtyCommand<FogChanged>>();
        kernel.registerCommand<SelectRequested, SelectCommand>();
        kernel.registerCommand<CameraNavigated, CameraSyncCommand>();

        // Document boundary + undo/redo themselves -- never wrapped.
        kernel.registerCommand<NewDocumentRequested, NewDocumentCommand>();
        kernel.registerCommand<OpenDocumentRequested, OpenDocumentCommand>();
        kernel.registerCommand<SaveDocumentRequested, SaveDocumentCommand>();
        kernel.registerCommand<UndoRequested, UndoCommand>();
        kernel.registerCommand<RedoRequested, RedoCommand>();

        geometry = kernel.agentAs<GeometryApi>(kGeometryApiName);
        tags = kernel.agentAs<TagStore>(kTagStoreName);
        guides = kernel.agentAs<GuideStore>(kGuideStoreName);
        annotations = kernel.agentAs<AnnotationStore>(kAnnotationStoreName);
        sections = kernel.agentAs<SectionStore>(kSectionStoreName);
        axes = kernel.agentAs<AxesStore>(kAxesStoreName);
        materials = kernel.agentAs<MaterialRepository>(kMaterialRepositoryName);
        style = kernel.agentAs<StyleStore>(kStyleStoreName);
        shadow = kernel.agentAs<ShadowStore>(kShadowStoreName);
        fog = kernel.agentAs<FogStore>(kFogStoreName);
        selection = kernel.agentAs<SelectionStore>(kSelectionStoreName);
        camera = kernel.agentAs<CameraStore>(kCameraStoreName);
        document = kernel.agentAs<DocumentStore>(kDocumentStoreName);
        undo = kernel.agentAs<UndoStore>(kUndoStoreName);
        ASSERT_NE(geometry, nullptr);
        ASSERT_NE(tags, nullptr);
        ASSERT_NE(guides, nullptr);
        ASSERT_NE(annotations, nullptr);
        ASSERT_NE(sections, nullptr);
        ASSERT_NE(axes, nullptr);
        ASSERT_NE(materials, nullptr);
        ASSERT_NE(style, nullptr);
        ASSERT_NE(shadow, nullptr);
        ASSERT_NE(fog, nullptr);
        ASSERT_NE(selection, nullptr);
        ASSERT_NE(camera, nullptr);
        ASSERT_NE(document, nullptr);
        ASSERT_NE(undo, nullptr);
    }

    // Byte-level document snapshot (excluding camera/meta, fixed constants
    // here -- undo/redo never touch the camera) via the same io::writeDocument
    // the .plr format uses.
    QByteArray bytesOf() const {
        const plnr::io::CameraState camState{};
        const plnr::io::DocumentMeta meta{"test", "", "in"};
        return plnr::io::writeDocument(*geometry, *tags, *guides, *annotations, *sections, *axes, *materials, *style,
                                       *shadow, *fog, camState, meta)
            .toJson(QJsonDocument::Compact);
    }

    std::string tempFilePath(const char* name) const {
        return QDir(tempDir.path()).filePath(QString::fromUtf8(name)).toStdString();
    }

    AppKernel kernel;
    QTemporaryDir tempDir;
    std::shared_ptr<GeometryApi> geometry;
    std::shared_ptr<TagStore> tags;
    std::shared_ptr<GuideStore> guides;
    std::shared_ptr<AnnotationStore> annotations;
    std::shared_ptr<SectionStore> sections;
    std::shared_ptr<AxesStore> axes;
    std::shared_ptr<MaterialRepository> materials;
    std::shared_ptr<StyleStore> style;
    std::shared_ptr<ShadowStore> shadow;
    std::shared_ptr<FogStore> fog;
    std::shared_ptr<SelectionStore> selection;
    std::shared_ptr<CameraStore> camera;
    std::shared_ptr<DocumentStore> document;
    std::shared_ptr<UndoStore> undo;
};

TEST_F(UndoTest, DrawRectangleUndoRedoByteIdentical) {
    kernel.send(AddRectangleRequested{{0, 0, 0}, {4, 3, 0}});
    ASSERT_EQ(geometry->model().vertices().size(), 4u);
    ASSERT_EQ(geometry->model().faces().size(), 1u);
    const QByteArray afterDraw = bytesOf();

    ASSERT_TRUE(undo->canUndo());
    kernel.send(UndoRequested{});
    EXPECT_TRUE(geometry->model().vertices().empty());
    EXPECT_FALSE(undo->canUndo());
    EXPECT_TRUE(undo->canRedo());

    kernel.send(RedoRequested{});
    EXPECT_EQ(geometry->model().vertices().size(), 4u);
    EXPECT_EQ(bytesOf(), afterDraw);
    EXPECT_TRUE(undo->canUndo());
    EXPECT_FALSE(undo->canRedo());
}

TEST_F(UndoTest, SequentialGesturesUndoInReverseOrder) {
    kernel.send(AddEdgeRequested{{0, 0, 0}, {1, 0, 0}});  // gesture 1
    ASSERT_EQ(geometry->model().vertices().size(), 2u);
    kernel.send(AddEdgeRequested{{5, 5, 0}, {6, 5, 0}});  // gesture 2, disjoint
    ASSERT_EQ(geometry->model().vertices().size(), 4u);
    ASSERT_EQ(geometry->model().edges().size(), 2u);

    kernel.send(UndoRequested{});  // undoes gesture 2
    EXPECT_EQ(geometry->model().vertices().size(), 2u);
    EXPECT_EQ(geometry->model().edges().size(), 1u);

    kernel.send(UndoRequested{});  // undoes gesture 1
    EXPECT_TRUE(geometry->model().vertices().empty());
    EXPECT_FALSE(undo->canUndo());

    kernel.send(RedoRequested{});  // redoes gesture 1
    EXPECT_EQ(geometry->model().vertices().size(), 2u);
    kernel.send(RedoRequested{});  // redoes gesture 2
    EXPECT_EQ(geometry->model().vertices().size(), 4u);
    EXPECT_EQ(geometry->model().edges().size(), 2u);
}

TEST_F(UndoTest, TagCreateAndAssignUndoRedoEach) {
    kernel.send(AddEdgeRequested{{0, 0, 0}, {1, 0, 0}});
    const Id vId = geometry->model().vertices().begin()->first;

    kernel.send(TagCreateRequested{"Wood"});
    ASSERT_EQ(tags->tags().size(), 2u);
    const std::uint64_t tagId = tags->tags()[1].id;

    kernel.send(TagAssignRequested{{EntityRef{EntityKind::Vertex, vId}}, tagId});
    ASSERT_EQ(tags->tagOf(EntityRef{EntityKind::Vertex, vId}), tagId);

    kernel.send(UndoRequested{});  // undoes the assign
    EXPECT_EQ(tags->tags().size(), 2u);
    EXPECT_NE(tags->tagOf(EntityRef{EntityKind::Vertex, vId}), tagId);

    kernel.send(UndoRequested{});  // undoes the tag creation
    EXPECT_EQ(tags->tags().size(), 1u);

    kernel.send(RedoRequested{});  // redoes tag creation
    EXPECT_EQ(tags->tags().size(), 2u);
    kernel.send(RedoRequested{});  // redoes the assign
    EXPECT_EQ(tags->tagOf(EntityRef{EntityKind::Vertex, vId}), tagId);
}

TEST_F(UndoTest, GuideAddUndoRedo) {
    kernel.send(AddGuidePointRequested{{1, 1, 1}});
    ASSERT_EQ(guides->guides().size(), 1u);

    kernel.send(UndoRequested{});
    EXPECT_TRUE(guides->guides().empty());

    kernel.send(RedoRequested{});
    ASSERT_EQ(guides->guides().size(), 1u);
    EXPECT_TRUE(guides->guides()[0].point.x == 1.0);
}

TEST_F(UndoTest, AxesSetUndoRedo) {
    const plnr::agent::Frame defaultFrame = axes->frame();

    kernel.send(SetAxesRequested{{1, 2, 3}, {0, 1, 0}, {1, 0, 0}});
    const plnr::agent::Frame setFrame = axes->frame();
    EXPECT_FALSE(setFrame == defaultFrame);

    kernel.send(UndoRequested{});
    EXPECT_TRUE(axes->frame() == defaultFrame);

    kernel.send(RedoRequested{});
    EXPECT_TRUE(axes->frame() == setFrame);
}

TEST_F(UndoTest, SectionAddUndoRedo) {
    kernel.send(AddSectionPlaneRequested{{0, 0, 1}, {0, 0, 1}, "Cut"});
    ASSERT_EQ(sections->planes().size(), 1u);

    kernel.send(UndoRequested{});
    EXPECT_TRUE(sections->planes().empty());

    kernel.send(RedoRequested{});
    ASSERT_EQ(sections->planes().size(), 1u);
    EXPECT_EQ(sections->planes()[0].name, "Cut");
}

TEST_F(UndoTest, DimensionAddUndoRedo) {
    kernel.send(AddEdgeRequested{{0, 0, 0}, {1, 0, 0}});
    ASSERT_EQ(geometry->model().vertices().size(), 2u);
    auto it = geometry->model().vertices().begin();
    const Id vA = it->first;
    ++it;
    const Id vB = it->first;

    kernel.send(AddDimensionRequested{vA, vB, {0, 1, 0}, 0.5});
    ASSERT_EQ(annotations->dimensions().size(), 1u);

    kernel.send(UndoRequested{});
    EXPECT_TRUE(annotations->dimensions().empty());

    kernel.send(RedoRequested{});
    ASSERT_EQ(annotations->dimensions().size(), 1u);
}

TEST_F(UndoTest, HideUndoRedo) {
    kernel.send(AddEdgeRequested{{0, 0, 0}, {1, 0, 0}});
    const Id vId = geometry->model().vertices().begin()->first;
    const EntityRef ref{EntityKind::Vertex, vId};

    kernel.send(SetHiddenRequested{{ref}, true});
    ASSERT_TRUE(geometry->isHidden(ref));

    kernel.send(UndoRequested{});
    EXPECT_FALSE(geometry->isHidden(ref));

    kernel.send(RedoRequested{});
    EXPECT_TRUE(geometry->isHidden(ref));
}

TEST_F(UndoTest, MakeGroupThenUndoIsCrossModel) {
    kernel.send(AddRectangleRequested{{0, 0, 0}, {4, 3, 0}});
    ASSERT_EQ(geometry->model().vertices().size(), 4u);
    ASSERT_EQ(geometry->model().edges().size(), 4u);
    ASSERT_EQ(geometry->model().faces().size(), 1u);
    const Id faceId = geometry->model().faces().begin()->first;

    kernel.send(SelectRequested{SelectMode::Replace, EntityRef{EntityKind::Face, faceId}, SelectExpand::None});
    ASSERT_FALSE(selection->empty());

    kernel.send(GroupCreateRequested{false, ""});
    EXPECT_TRUE(geometry->model().vertices().empty());  // moved out of root
    ASSERT_EQ(geometry->scene().root().children.size(), 1u);
    const Id instanceId = geometry->scene().root().children[0].id;
    const Id defId = geometry->scene().root().children[0].definitionId;
    const plnr::geo::Definition* def = geometry->scene().definition(defId);
    ASSERT_NE(def, nullptr);
    EXPECT_EQ(def->model.vertices().size(), 4u);
    EXPECT_EQ(def->model.faces().size(), 1u);

    kernel.send(UndoRequested{});
    EXPECT_EQ(geometry->model().vertices().size(), 4u);  // back in root
    EXPECT_EQ(geometry->model().edges().size(), 4u);
    EXPECT_EQ(geometry->model().faces().size(), 1u);
    EXPECT_TRUE(geometry->scene().root().children.empty());  // instance gone
    EXPECT_EQ(geometry->scene().definition(defId), nullptr);  // definition gone

    kernel.send(RedoRequested{});
    EXPECT_TRUE(geometry->model().vertices().empty());
    ASSERT_EQ(geometry->scene().root().children.size(), 1u);
    EXPECT_EQ(geometry->scene().root().children[0].id, instanceId);
    EXPECT_EQ(geometry->scene().root().children[0].definitionId, defId);
    const plnr::geo::Definition* redoneDef = geometry->scene().definition(defId);
    ASSERT_NE(redoneDef, nullptr);
    EXPECT_EQ(redoneDef->model.vertices().size(), 4u);
}

TEST_F(UndoTest, ExplodeThenUndo) {
    kernel.send(AddRectangleRequested{{0, 0, 0}, {4, 3, 0}});
    const Id faceId = geometry->model().faces().begin()->first;
    kernel.send(SelectRequested{SelectMode::Replace, EntityRef{EntityKind::Face, faceId}, SelectExpand::None});
    kernel.send(GroupCreateRequested{false, ""});
    ASSERT_EQ(geometry->scene().root().children.size(), 1u);
    const Id instanceId = geometry->scene().root().children[0].id;
    ASSERT_TRUE(geometry->model().vertices().empty());

    kernel.send(ExplodeRequested{instanceId});
    EXPECT_EQ(geometry->model().vertices().size(), 4u);  // back in root
    EXPECT_TRUE(geometry->scene().root().children.empty());  // instance gone

    kernel.send(UndoRequested{});
    EXPECT_TRUE(geometry->model().vertices().empty());  // back in the definition
    ASSERT_EQ(geometry->scene().root().children.size(), 1u);
    EXPECT_EQ(geometry->scene().root().children[0].id, instanceId);

    kernel.send(RedoRequested{});
    EXPECT_EQ(geometry->model().vertices().size(), 4u);
    EXPECT_TRUE(geometry->scene().root().children.empty());
}

TEST_F(UndoTest, MoveInstanceUndoRedo) {
    kernel.send(AddRectangleRequested{{0, 0, 0}, {4, 3, 0}});
    const Id faceId = geometry->model().faces().begin()->first;
    kernel.send(SelectRequested{SelectMode::Replace, EntityRef{EntityKind::Face, faceId}, SelectExpand::None});
    kernel.send(GroupCreateRequested{false, ""});
    ASSERT_EQ(geometry->scene().root().children.size(), 1u);
    const Id instanceId = geometry->scene().root().children[0].id;
    const plnr::geo::Transform before = geometry->scene().findInstance(kRootDefinitionId, instanceId)->transform;

    kernel.send(MoveEntityRequested{EntityKind::Instance, instanceId, {10.0, 0.0, 0.0}});
    const plnr::geo::Transform moved = geometry->scene().findInstance(kRootDefinitionId, instanceId)->transform;
    EXPECT_FALSE(moved.almostEqual(before));
    EXPECT_TRUE(plnr::geo::almostEqual(moved.t, plnr::geo::Vec3{10.0, 0.0, 0.0}));

    kernel.send(UndoRequested{});
    EXPECT_TRUE(geometry->scene().findInstance(kRootDefinitionId, instanceId)->transform.almostEqual(before));

    kernel.send(RedoRequested{});
    EXPECT_TRUE(geometry->scene().findInstance(kRootDefinitionId, instanceId)->transform.almostEqual(moved));
}

TEST_F(UndoTest, RejectedCommandCreatesNoStep) {
    kernel.send(AddEdgeRequested{{0, 0, 0}, {0, 0, 0}});  // a == b: rejected, no mutation
    EXPECT_TRUE(geometry->model().vertices().empty());
    EXPECT_FALSE(undo->canUndo());
}

TEST_F(UndoTest, RedoClearedByNewMutation) {
    kernel.send(AddEdgeRequested{{0, 0, 0}, {1, 0, 0}});
    kernel.send(UndoRequested{});
    ASSERT_TRUE(undo->canRedo());

    kernel.send(AddEdgeRequested{{5, 5, 0}, {6, 5, 0}});  // a brand-new mutation
    EXPECT_FALSE(undo->canRedo());
}

TEST(UndoStoreCapTest, CapEvictionDropsOldestEntry) {
    AppKernel kernel;
    kernel.registerAgent(std::make_shared<GeometryApi>());
    kernel.registerAgent(std::make_shared<UndoStore>(/*cap=*/2));
    kernel.registerCommand<AddEdgeRequested, UndoCaptureCommand<AddEdgeCommand, AddEdgeRequested>>();
    kernel.registerCommand<UndoRequested, UndoCommand>();

    auto undo = kernel.agentAs<UndoStore>(kUndoStoreName);
    ASSERT_NE(undo, nullptr);

    kernel.send(AddEdgeRequested{{0, 0, 0}, {1, 0, 0}});
    kernel.send(AddEdgeRequested{{2, 0, 0}, {3, 0, 0}});
    kernel.send(AddEdgeRequested{{4, 0, 0}, {5, 0, 0}});  // 3 gestures pushed onto a cap-2 stack

    ASSERT_TRUE(undo->canUndo());
    kernel.send(UndoRequested{});
    ASSERT_TRUE(undo->canUndo());
    kernel.send(UndoRequested{});
    // Only 2 entries were ever retained (the oldest, gesture 1, was evicted
    // when gesture 3 was pushed) -- a third undo has nothing left to pop.
    EXPECT_FALSE(undo->canUndo());
}

TEST_F(UndoTest, NewDocumentClearsUndoStacks) {
    kernel.send(AddEdgeRequested{{0, 0, 0}, {1, 0, 0}});
    ASSERT_TRUE(undo->canUndo());

    kernel.send(NewDocumentRequested{});
    EXPECT_FALSE(undo->canUndo());
    EXPECT_FALSE(undo->canRedo());
}

TEST_F(UndoTest, OpenDocumentClearsUndoStacks) {
    ASSERT_TRUE(tempDir.isValid());
    const std::string path = tempFilePath("model.plr");
    kernel.send(SaveDocumentRequested{path});

    kernel.send(AddEdgeRequested{{0, 0, 0}, {1, 0, 0}});
    kernel.send(UndoRequested{});
    ASSERT_TRUE(undo->canRedo());

    kernel.send(OpenDocumentRequested{path});
    EXPECT_FALSE(undo->canUndo());
    EXPECT_FALSE(undo->canRedo());
}

TEST_F(UndoTest, UndoLeavesCameraStoreUntouched) {
    kernel.send(CameraNavigated{{5, 6, 7}, 12.0, 34.0, 56.0, 40.0});
    ASSERT_EQ(camera->state().target.x, 5.0);

    kernel.send(AddEdgeRequested{{0, 0, 0}, {1, 0, 0}});
    kernel.send(UndoRequested{});

    EXPECT_EQ(camera->state().target.x, 5.0);
    EXPECT_EQ(camera->state().azimuthDeg, 12.0);
    EXPECT_EQ(camera->state().distance, 56.0);
}

TEST_F(UndoTest, UndoMarksDocumentDirty) {
    ASSERT_TRUE(tempDir.isValid());
    kernel.send(AddEdgeRequested{{0, 0, 0}, {1, 0, 0}});
    const std::string path = tempFilePath("model.plr");
    kernel.send(SaveDocumentRequested{path});
    ASSERT_FALSE(document->dirty());

    kernel.send(AddEdgeRequested{{5, 5, 0}, {6, 5, 0}});
    kernel.send(SaveDocumentRequested{path});
    ASSERT_FALSE(document->dirty());

    kernel.send(UndoRequested{});
    EXPECT_TRUE(document->dirty());
}

// Style changes are dirty-tracked but DELIBERATELY excluded from undo
// capture -- proven two ways: (a) a style-only change still marks dirty,
// (b) a style change after a real undo op does NOT push its own undo step.
TEST_F(UndoTest, StyleChangeIsDirtyButNeverCreatesItsOwnUndoStep) {
    ASSERT_TRUE(tempDir.isValid());
    const std::string path = tempFilePath("model.plr");
    kernel.send(SaveDocumentRequested{path});
    ASSERT_FALSE(document->dirty());

    // (a) style-only change marks dirty with nothing else in play.
    kernel.send(SetFaceStyleRequested{FaceStyle::XRay});
    EXPECT_EQ(style->faceStyle(), FaceStyle::XRay);
    EXPECT_TRUE(document->dirty());
    EXPECT_FALSE(undo->canUndo());  // no undo-worthy op has happened yet

    kernel.send(SaveDocumentRequested{path});
    ASSERT_FALSE(document->dirty());

    // (b) a real undoable op, THEN a style change -- undo must revert only
    // the geometry, leaving style untouched, and canUndo() must go straight
    // to false (proving the style change never pushed a second step).
    kernel.send(AddEdgeRequested{{0, 0, 0}, {1, 0, 0}});
    ASSERT_TRUE(undo->canUndo());
    kernel.send(SetFaceStyleRequested{FaceStyle::Monochrome});
    kernel.send(SetEdgeStyleFlagRequested{EdgeFlag::Profiles, true});
    EXPECT_EQ(style->faceStyle(), FaceStyle::Monochrome);
    EXPECT_TRUE(style->profiles());

    kernel.send(UndoRequested{});
    EXPECT_TRUE(geometry->model().vertices().empty());  // the edge is gone
    EXPECT_FALSE(undo->canUndo());                       // exactly one step existed
    EXPECT_EQ(style->faceStyle(), FaceStyle::Monochrome);  // style survives the undo, unchanged
    EXPECT_TRUE(style->profiles());
}

// Same "dirty yes, undo no" contract as
// StyleChangeIsDirtyButNeverCreatesItsOwnUndoStep, proven the identical two
// ways for ShadowStore's own events.
TEST_F(UndoTest, ShadowChangeIsDirtyButNeverCreatesItsOwnUndoStep) {
    ASSERT_TRUE(tempDir.isValid());
    const std::string path = tempFilePath("model.plr");
    kernel.send(SaveDocumentRequested{path});
    ASSERT_FALSE(document->dirty());

    // (a) shadow-only change marks dirty with nothing else in play. OFF is
    // useSunForShading's non-default value -- true would no-op instead of
    // dirtying.
    kernel.send(SetUseSunForShadingRequested{false});
    kernel.send(SetShowShadowsRequested{true});
    EXPECT_FALSE(shadow->useSunForShading());
    EXPECT_TRUE(shadow->showShadows());
    EXPECT_TRUE(document->dirty());
    EXPECT_FALSE(undo->canUndo());  // no undo-worthy op has happened yet

    kernel.send(SaveDocumentRequested{path});
    ASSERT_FALSE(document->dirty());

    // (b) a real undoable op, THEN shadow changes -- undo must revert only
    // the geometry, leaving shadow settings untouched, canUndo() straight
    // to false.
    kernel.send(AddEdgeRequested{{0, 0, 0}, {1, 0, 0}});
    ASSERT_TRUE(undo->canUndo());
    kernel.send(SetSunPositionRequested{51.48, -0.08});
    kernel.send(SetSunDateTimeRequested{6, 21, 9.0});
    EXPECT_EQ(shadow->latitudeDeg(), 51.48);
    EXPECT_EQ(shadow->month(), 6);

    kernel.send(UndoRequested{});
    EXPECT_TRUE(geometry->model().vertices().empty());  // the edge is gone
    EXPECT_FALSE(undo->canUndo());                       // exactly one step existed
    EXPECT_EQ(shadow->latitudeDeg(), 51.48);  // shadow settings survive the undo, unchanged
    EXPECT_EQ(shadow->month(), 6);
}

// Same "dirty yes, undo no" contract as
// ShadowChangeIsDirtyButNeverCreatesItsOwnUndoStep, proven the identical two
// ways for FogStore's own events.
TEST_F(UndoTest, FogChangeIsDirtyButNeverCreatesItsOwnUndoStep) {
    ASSERT_TRUE(tempDir.isValid());
    const std::string path = tempFilePath("model.plr");
    kernel.send(SaveDocumentRequested{path});
    ASSERT_FALSE(document->dirty());

    // (a) fog-only change marks dirty with nothing else in play.
    kernel.send(SetFogEnabledRequested{true});
    EXPECT_TRUE(fog->enabled());
    EXPECT_TRUE(document->dirty());
    EXPECT_FALSE(undo->canUndo());  // no undo-worthy op has happened yet

    kernel.send(SaveDocumentRequested{path});
    ASSERT_FALSE(document->dirty());

    // (b) a real undoable op, THEN fog changes -- undo must revert only the
    // geometry, leaving fog settings untouched, canUndo() straight to false.
    kernel.send(AddEdgeRequested{{0, 0, 0}, {1, 0, 0}});
    ASSERT_TRUE(undo->canUndo());
    kernel.send(SetFogRangeRequested{10.0, 80.0});
    kernel.send(SetFogUseBackgroundColorRequested{false});
    EXPECT_EQ(fog->startDistance(), 10.0);
    EXPECT_FALSE(fog->useBackgroundColor());

    kernel.send(UndoRequested{});
    EXPECT_TRUE(geometry->model().vertices().empty());  // the edge is gone
    EXPECT_FALSE(undo->canUndo());                       // exactly one step existed
    EXPECT_EQ(fog->startDistance(), 10.0);  // fog settings survive the undo, unchanged
    EXPECT_FALSE(fog->useBackgroundColor());
}

TEST_F(UndoTest, UndoStateChangedTransitionCounts) {
    int count = 0;
    kernel.dispatcher().subscribe<UndoStateChanged>(&count, [&count](const UndoStateChanged&) { ++count; });

    kernel.send(AddEdgeRequested{{0, 0, 0}, {1, 0, 0}});  // canUndo: false -> true
    EXPECT_EQ(count, 1);

    kernel.send(UndoRequested{});  // canUndo: true -> false, canRedo: false -> true
    EXPECT_EQ(count, 2);

    kernel.send(RedoRequested{});  // canUndo: false -> true, canRedo: true -> false
    EXPECT_EQ(count, 3);
}

// -- Unit-level TransactionManager test: drives begin()/abort() directly
// with PLAIN (unwrapped) registrations -- wrapping AddEdgeRequested etc.
// would nest a second TransactionManager inside this one. ----------------
TEST(TransactionManagerTest, AbortRestoresMidTransactionState) {
    AppKernel kernel;
    kernel.registerAgent(std::make_shared<GeometryApi>());
    kernel.registerAgent(std::make_shared<TagStore>());
    kernel.registerAgent(std::make_shared<GuideStore>());
    kernel.registerAgent(std::make_shared<AnnotationStore>());
    kernel.registerAgent(std::make_shared<SectionStore>());
    kernel.registerAgent(std::make_shared<AxesStore>());
    kernel.registerAgent(std::make_shared<SelectionStore>());
    kernel.registerAgent(std::make_shared<EditContextStore>());
    kernel.registerAgent(std::make_shared<CameraStore>());
    kernel.registerAgent(std::make_shared<DocumentStore>());
    kernel.registerAgent(std::make_shared<UndoStore>());

    kernel.registerCommand<AddEdgeRequested, AddEdgeCommand>();
    kernel.registerCommand<TagCreateRequested, TagCreateCommand>();
    kernel.registerCommand<SetHiddenRequested, SetHiddenCommand>();
    kernel.registerCommand<AddGuidePointRequested, AddGuidePointCommand>();
    kernel.registerCommand<GeometryChanged, GeometryChangedCommand>();
    kernel.registerCommand<TagsChanged, MarkDirtyCommand<TagsChanged>>();
    kernel.registerCommand<GuidesChanged, MarkDirtyCommand<GuidesChanged>>();

    auto geometry = kernel.agentAs<GeometryApi>(kGeometryApiName);
    auto tags = kernel.agentAs<TagStore>(kTagStoreName);
    auto guides = kernel.agentAs<GuideStore>(kGuideStoreName);
    auto undo = kernel.agentAs<UndoStore>(kUndoStoreName);
    ASSERT_NE(geometry, nullptr);
    ASSERT_NE(tags, nullptr);
    ASSERT_NE(guides, nullptr);
    ASSERT_NE(undo, nullptr);

    TransactionManager txn(kernel);
    txn.begin();
    ASSERT_TRUE(txn.isActive());

    kernel.send(AddEdgeRequested{{0, 0, 0}, {1, 0, 0}});
    kernel.send(TagCreateRequested{"Wood"});
    kernel.send(AddGuidePointRequested{{2, 2, 2}});
    const Id vId = geometry->model().vertices().begin()->first;
    kernel.send(SetHiddenRequested{{EntityRef{EntityKind::Vertex, vId}}, true});

    ASSERT_FALSE(geometry->model().vertices().empty());
    ASSERT_EQ(tags->tags().size(), 2u);
    ASSERT_EQ(guides->guides().size(), 1u);
    ASSERT_TRUE(geometry->isHidden(EntityRef{EntityKind::Vertex, vId}));

    txn.abort();

    EXPECT_FALSE(txn.isActive());
    EXPECT_TRUE(geometry->model().vertices().empty());
    EXPECT_EQ(tags->tags().size(), 1u);
    EXPECT_TRUE(guides->guides().empty());
    EXPECT_TRUE(geometry->hidden().empty());
    EXPECT_FALSE(undo->canUndo());  // abort() never pushes an undo step
}

}  // namespace
