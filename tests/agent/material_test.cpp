#include "agent/material_repository.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QIODevice>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QTemporaryDir>

#include <ordo/core/kernel.h>

#include "agent/command/annotation_commands.h"
#include "agent/annotation_store.h"
#include "agent/asset_repository.h"
#include "agent/axes_store.h"
#include "agent/camera_store.h"
#include "agent/command/document_commands.h"
#include "agent/document_store.h"
#include "agent/edit_context_store.h"
#include "agent/events.h"
#include "agent/fog_store.h"
#include "agent/command/geometry_commands.h"
#include "agent/geometry_api.h"
#include "agent/command/group_commands.h"
#include "agent/guide_store.h"
#include "agent/command/material_commands.h"
#include "agent/command/material_texture_commands.h"
#include "agent/section_store.h"
#include "agent/command/selection_commands.h"
#include "agent/selection_store.h"
#include "agent/shadow_store.h"
#include "agent/command/style_commands.h"
#include "agent/style_store.h"
#include "agent/tag_store.h"
#include "agent/transaction.h"
#include "agent/command/undo_commands.h"
#include "agent/undo_store.h"
#include "io/plr_reader.h"
#include "io/plr_writer.h"

#include <geo/entity.h>
#include <geo/model.h>
#include <geo/scene.h>

#include <gtest/gtest.h>

namespace {

using ordo::core::Kernel;
using plnr::agent::AddEdgeCommand;
using plnr::agent::AddRectangleCommand;
using plnr::agent::AnnotationStore;
using plnr::agent::AssetRepository;
using plnr::agent::AxesStore;
using plnr::agent::CameraStore;
using plnr::agent::CameraSyncCommand;
using plnr::agent::DocumentStore;
using plnr::agent::EditContextStore;
using plnr::agent::FogStore;
using plnr::agent::GeometryChangedCommand;
using plnr::agent::GeometryApi;
using plnr::agent::GroupCreateCommand;
using plnr::agent::GuideStore;
using plnr::agent::kAnnotationStoreName;
using plnr::agent::kAssetRepositoryName;
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
using plnr::agent::Material;
using plnr::agent::MaterialCreateCommand;
using plnr::agent::MaterialEditCommand;
using plnr::agent::MaterialSetTextureCommand;
using plnr::agent::MaterialRepository;
using plnr::agent::SetUvTransformCommand;
using plnr::agent::NewDocumentCommand;
using plnr::agent::OpenDocumentCommand;
using plnr::agent::PaintCommand;
using plnr::agent::RedoCommand;
using plnr::agent::RemoveEdgeCommand;
using plnr::agent::SaveDocumentCommand;
using plnr::agent::SectionStore;
using plnr::agent::SelectCommand;
using plnr::agent::SelectionStore;
using plnr::agent::SetActiveMaterialCommand;
using plnr::agent::ShadowStore;
using plnr::agent::StyleStore;
using plnr::agent::TagStore;
using plnr::agent::UndoCaptureCommand;
using plnr::agent::UndoCommand;
using plnr::agent::UndoStore;
using plnr::events::AddEdgeRequested;
using plnr::events::AddRectangleRequested;
using plnr::events::AnnotationsChanged;
using plnr::events::AxesChanged;
using plnr::events::CameraNavigated;
using plnr::events::DocumentIoFailed;
using plnr::events::EntityRef;
using plnr::events::GeometryChanged;
using plnr::events::GroupCreateRequested;
using plnr::events::GuidesChanged;
using plnr::events::MaterialCreateRequested;
using plnr::events::MaterialEditRequested;
using plnr::events::MaterialSetTextureRequested;
using plnr::events::MaterialsChanged;
using plnr::events::NewDocumentRequested;
using plnr::events::OpenDocumentRequested;
using plnr::events::PaintRequested;
using plnr::events::RedoRequested;
using plnr::events::RemoveEdgeRequested;
using plnr::events::SaveDocumentRequested;
using plnr::events::SectionsChanged;
using plnr::events::SelectExpand;
using plnr::events::SelectMode;
using plnr::events::SelectRequested;
using plnr::events::SetActiveMaterialRequested;
using plnr::events::SetUvTransformRequested;
using plnr::events::TagsChanged;
using plnr::events::UndoRequested;
using plnr::events::UvTransform;
using plnr::geo::EntityKind;
using plnr::geo::Id;
// plnr::io::CameraState is deliberately not aliased here -- it would collide
// with plnr::agent::CameraState's identical unqualified name; fully-qualify
// io::CameraState at each call site instead.

// -- Shared fixture: a full kernel with every agent New/Open/Save/undo touch,
// used for both direct agent-method-call tests (agent rules) and real-event
// tests (undo/redo, dirty tracking, .plr round trip).
class MaterialTest : public ::testing::Test {
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

        // Mutating Intents -- wrapped, same as main.cpp; only the subset these tests drive.
        kernel.registerCommand<AddEdgeRequested, UndoCaptureCommand<AddEdgeCommand, AddEdgeRequested>>();
        kernel.registerCommand<AddRectangleRequested, UndoCaptureCommand<AddRectangleCommand, AddRectangleRequested>>();
        kernel.registerCommand<RemoveEdgeRequested, UndoCaptureCommand<RemoveEdgeCommand, RemoveEdgeRequested>>();
        kernel.registerCommand<GroupCreateRequested, UndoCaptureCommand<GroupCreateCommand, GroupCreateRequested>>();
        kernel.registerCommand<MaterialCreateRequested,
                                UndoCaptureCommand<MaterialCreateCommand, MaterialCreateRequested>>();
        kernel.registerCommand<MaterialEditRequested, UndoCaptureCommand<MaterialEditCommand, MaterialEditRequested>>();
        kernel.registerCommand<PaintRequested, UndoCaptureCommand<PaintCommand, PaintRequested>>();
        // SetActiveMaterialRequested is NOT wrapped -- transient UI-adjacent
        // state, never undo-worthy (same rule as main.cpp).
        kernel.registerCommand<SetActiveMaterialRequested, SetActiveMaterialCommand>();
        kernel.registerCommand<
            MaterialSetTextureRequested,
            UndoCaptureCommand<MaterialSetTextureCommand, MaterialSetTextureRequested>>();
        kernel.registerCommand<SetUvTransformRequested,
                                UndoCaptureCommand<SetUvTransformCommand, SetUvTransformRequested>>();

        // *Changed reactions (dirty tracking + aux-touch ping) and Select
        // (view-state) are never wrapped, same as main.cpp.
        kernel.registerCommand<GeometryChanged, GeometryChangedCommand>();
        kernel.registerCommand<TagsChanged, MarkDirtyCommand<TagsChanged>>();
        kernel.registerCommand<GuidesChanged, MarkDirtyCommand<GuidesChanged>>();
        kernel.registerCommand<AnnotationsChanged, MarkDirtyCommand<AnnotationsChanged>>();
        kernel.registerCommand<SectionsChanged, MarkDirtyCommand<SectionsChanged>>();
        kernel.registerCommand<AxesChanged, MarkDirtyCommand<AxesChanged>>();
        kernel.registerCommand<MaterialsChanged, MarkDirtyCommand<MaterialsChanged>>();
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
        assets = kernel.agentAs<AssetRepository>(kAssetRepositoryName);
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
        ASSERT_NE(assets, nullptr);
        ASSERT_NE(style, nullptr);
        ASSERT_NE(shadow, nullptr);
        ASSERT_NE(fog, nullptr);
        ASSERT_NE(selection, nullptr);
        ASSERT_NE(camera, nullptr);
        ASSERT_NE(document, nullptr);
        ASSERT_NE(undo, nullptr);
    }

    std::string tempFilePath(const char* name) const {
        return QDir(tempDir.path()).filePath(QString::fromUtf8(name)).toStdString();
    }

    Kernel kernel;
    QTemporaryDir tempDir;
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
    std::shared_ptr<CameraStore> camera;
    std::shared_ptr<DocumentStore> document;
    std::shared_ptr<UndoStore> undo;
};

// -- Agent-level rules (direct agent method calls, not via kernel.send) ----

TEST_F(MaterialTest, CreateAutoNamesWithNewIdAndFiresOnce) {
    int changed = 0;
    kernel.dispatcher().subscribe<MaterialsChanged>(&changed, [&changed](const MaterialsChanged&) { ++changed; });

    const Id id = materials->create("", 0.2, 0.4, 0.6, 1.0);
    ASSERT_EQ(materials->materials().size(), 1u);
    EXPECT_EQ(materials->materials()[0].name, "Material " + std::to_string(id));
    EXPECT_EQ(changed, 1);
}

TEST_F(MaterialTest, CreateWithExplicitNameKeepsIt) {
    materials->create("Wood", 0.5, 0.3, 0.1, 1.0);
    ASSERT_EQ(materials->materials().size(), 1u);
    EXPECT_EQ(materials->materials()[0].name, "Wood");
}

TEST_F(MaterialTest, EditUnknownIdIsNoOp) {
    int changed = 0;
    kernel.dispatcher().subscribe<MaterialsChanged>(&changed, [&changed](const MaterialsChanged&) { ++changed; });

    EXPECT_FALSE(materials->edit(999, "X", 0, 0, 0, 1.0));
    EXPECT_EQ(changed, 0);
}

TEST_F(MaterialTest, EditWithIdenticalValuesIsNoOp) {
    const Id id = materials->create("Wood", 0.5, 0.3, 0.1, 1.0);
    int changed = 0;
    kernel.dispatcher().subscribe<MaterialsChanged>(&changed, [&changed](const MaterialsChanged&) { ++changed; });

    EXPECT_FALSE(materials->edit(id, "Wood", 0.5, 0.3, 0.1, 1.0));
    EXPECT_EQ(changed, 0);
}

TEST_F(MaterialTest, EditChangesFieldsAndFiresOnce) {
    const Id id = materials->create("Wood", 0.5, 0.3, 0.1, 1.0);
    int changed = 0;
    kernel.dispatcher().subscribe<MaterialsChanged>(&changed, [&changed](const MaterialsChanged&) { ++changed; });

    EXPECT_TRUE(materials->edit(id, "Oak", 0.6, 0.4, 0.2, 0.8));
    ASSERT_NE(materials->material(id), nullptr);
    EXPECT_EQ(materials->material(id)->name, "Oak");
    EXPECT_EQ(materials->material(id)->opacity, 0.8);
    EXPECT_EQ(changed, 1);
}

TEST_F(MaterialTest, PaintSetsFrontSlotAndFiresOnce) {
    const Id matId = materials->create("Wood", 0.5, 0.3, 0.1, 1.0);
    const EntityRef ref{EntityKind::Face, 42};
    int changed = 0;
    kernel.dispatcher().subscribe<MaterialsChanged>(&changed, [&changed](const MaterialsChanged&) { ++changed; });

    EXPECT_TRUE(materials->paint({ref}, matId));
    const auto* assign = materials->assignment(ref);
    ASSERT_NE(assign, nullptr);
    EXPECT_EQ(assign->frontMaterialId, matId);
    EXPECT_EQ(assign->backMaterialId, 0u);
    EXPECT_EQ(changed, 1);
}

TEST_F(MaterialTest, PaintSameValueTwiceIsNoOpSecondTime) {
    const Id matId = materials->create("Wood", 0.5, 0.3, 0.1, 1.0);
    const EntityRef ref{EntityKind::Face, 42};
    ASSERT_TRUE(materials->paint({ref}, matId));

    int changed = 0;
    kernel.dispatcher().subscribe<MaterialsChanged>(&changed, [&changed](const MaterialsChanged&) { ++changed; });
    EXPECT_FALSE(materials->paint({ref}, matId));
    EXPECT_EQ(changed, 0);
}

TEST_F(MaterialTest, PaintZeroClearsAndErasesEntryWhenBackAlsoZero) {
    const Id matId = materials->create("Wood", 0.5, 0.3, 0.1, 1.0);
    const EntityRef ref{EntityKind::Face, 42};
    ASSERT_TRUE(materials->paint({ref}, matId));

    ASSERT_TRUE(materials->paint({ref}, 0));
    EXPECT_EQ(materials->assignment(ref), nullptr);
    EXPECT_TRUE(materials->assignments().empty());
}

TEST_F(MaterialTest, PaintUnknownMaterialIdRejectsWholeCallWithNoMutation) {
    const EntityRef ref{EntityKind::Face, 42};
    EXPECT_FALSE(materials->paint({ref}, 424242));
    EXPECT_EQ(materials->assignment(ref), nullptr);
}

TEST_F(MaterialTest, PaintInstanceRefUsesFrontSlotOnly) {
    const Id matId = materials->create("Wood", 0.5, 0.3, 0.1, 1.0);
    const EntityRef instRef{EntityKind::Instance, 7};

    ASSERT_TRUE(materials->paint({instRef}, matId));
    const auto* assign = materials->assignment(instRef);
    ASSERT_NE(assign, nullptr);
    EXPECT_EQ(assign->frontMaterialId, matId);
    EXPECT_EQ(assign->backMaterialId, 0u);
}

// MaterialRepository is deliberately dumb about scene/model validity -- an
// EntityRef naming an entity that never existed is accepted verbatim (same
// "stale reference is legal" contract TagStore/GeometryApi use).
TEST_F(MaterialTest, PaintToleratesAssignmentOnEntityThatNeverExistedInAnyModel) {
    const Id matId = materials->create("Ghost", 1, 1, 1, 1.0);
    const EntityRef staleRef{EntityKind::Face, 999999};

    EXPECT_TRUE(materials->paint({staleRef}, matId));
    EXPECT_NE(materials->assignment(staleRef), nullptr);
}

TEST_F(MaterialTest, RestoreMaterialRejectsZeroIdAndDuplicateId) {
    EXPECT_FALSE(materials->restoreMaterial(Material{0, "Bad", 0, 0, 0, 1.0}));
    ASSERT_TRUE(materials->restoreMaterial(Material{5, "Five", 0, 0, 0, 1.0}));
    EXPECT_FALSE(materials->restoreMaterial(Material{5, "Dup", 1, 1, 1, 1.0}));  // id reuse rejected
}

TEST_F(MaterialTest, RestoreMaterialFoldsNextIdSoSubsequentCreateAvoidsCollision) {
    ASSERT_TRUE(materials->restoreMaterial(Material{5, "Five", 0, 0, 0, 1.0}));
    const Id newId = materials->create("Six", 0, 0, 0, 1.0);
    EXPECT_EQ(newId, 6u);
}

TEST_F(MaterialTest, RestoreAssignmentRejectsUnknownMaterialIdOnEitherSlot) {
    const Id matId = materials->create("Wood", 0.5, 0.3, 0.1, 1.0);
    const EntityRef ref{EntityKind::Face, 1};
    EXPECT_FALSE(materials->restoreAssignment(ref, 424242, 0));
    EXPECT_FALSE(materials->restoreAssignment(ref, matId, 424242));
    EXPECT_TRUE(materials->restoreAssignment(ref, matId, 0));
}

// -- Per-face UV transform --

TEST_F(MaterialTest, SetUvTransformSetsFieldsAndFiresOnce) {
    const Id matId = materials->create("Wood", 0.5, 0.3, 0.1, 1.0);
    const EntityRef ref{EntityKind::Face, 1};
    ASSERT_TRUE(materials->paint({ref}, matId));

    int changed = 0;
    kernel.dispatcher().subscribe<MaterialsChanged>(&changed, [&changed](const MaterialsChanged&) { ++changed; });

    const UvTransform t{0.5, -0.25, 0.3, 1.5, 0.8};
    EXPECT_TRUE(materials->setUvTransform(ref, t));
    const auto* assign = materials->assignment(ref);
    ASSERT_NE(assign, nullptr);
    EXPECT_EQ(assign->uvTransform.offsetU, 0.5);
    EXPECT_EQ(assign->uvTransform.offsetV, -0.25);
    EXPECT_EQ(assign->uvTransform.rotationRad, 0.3);
    EXPECT_EQ(assign->uvTransform.scaleU, 1.5);
    EXPECT_EQ(assign->uvTransform.scaleV, 0.8);
    EXPECT_EQ(changed, 1);
}

TEST_F(MaterialTest, SetUvTransformOnRefWithNoAssignmentIsNoOp) {
    const EntityRef ref{EntityKind::Face, 1};
    int changed = 0;
    kernel.dispatcher().subscribe<MaterialsChanged>(&changed, [&changed](const MaterialsChanged&) { ++changed; });

    EXPECT_FALSE(materials->setUvTransform(ref, UvTransform{0.5, 0.5, 0.0, 1.0, 1.0}));
    EXPECT_EQ(materials->assignment(ref), nullptr);
    EXPECT_EQ(changed, 0);
}

TEST_F(MaterialTest, SetUvTransformWithIdenticalValuesIsNoOp) {
    const Id matId = materials->create("Wood", 0.5, 0.3, 0.1, 1.0);
    const EntityRef ref{EntityKind::Face, 1};
    ASSERT_TRUE(materials->paint({ref}, matId));
    const UvTransform t{0.5, -0.25, 0.3, 1.5, 0.8};
    ASSERT_TRUE(materials->setUvTransform(ref, t));

    int changed = 0;
    kernel.dispatcher().subscribe<MaterialsChanged>(&changed, [&changed](const MaterialsChanged&) { ++changed; });
    EXPECT_FALSE(materials->setUvTransform(ref, t));
    EXPECT_EQ(changed, 0);
}

// Setting BACK to identity is an ORDINARY call through the same path (no
// separate clear method, see MaterialRepository::setUvTransform's own comment) --
// still fires MaterialsChanged like any other real value change.
TEST_F(MaterialTest, SetUvTransformBackToIdentityFiresOnce) {
    const Id matId = materials->create("Wood", 0.5, 0.3, 0.1, 1.0);
    const EntityRef ref{EntityKind::Face, 1};
    ASSERT_TRUE(materials->paint({ref}, matId));
    ASSERT_TRUE(materials->setUvTransform(ref, UvTransform{0.5, -0.25, 0.3, 1.5, 0.8}));

    int changed = 0;
    kernel.dispatcher().subscribe<MaterialsChanged>(&changed, [&changed](const MaterialsChanged&) { ++changed; });
    EXPECT_TRUE(materials->setUvTransform(ref, UvTransform{}));
    const auto* assign = materials->assignment(ref);
    ASSERT_NE(assign, nullptr);
    EXPECT_EQ(assign->uvTransform.offsetU, 0.0);
    EXPECT_EQ(assign->uvTransform.scaleU, 1.0);
    EXPECT_EQ(changed, 1);
}

TEST_F(MaterialTest, RestoreAssignmentFourArgOverloadRoundTripsUvTransform) {
    const Id matId = materials->create("Wood", 0.5, 0.3, 0.1, 1.0);
    const EntityRef ref{EntityKind::Face, 1};
    const UvTransform t{0.1, 0.2, 0.3, 1.1, 1.2};
    EXPECT_TRUE(materials->restoreAssignment(ref, matId, 0, t));
    const auto* assign = materials->assignment(ref);
    ASSERT_NE(assign, nullptr);
    EXPECT_EQ(assign->uvTransform.offsetU, 0.1);
    EXPECT_EQ(assign->uvTransform.offsetV, 0.2);
    EXPECT_EQ(assign->uvTransform.rotationRad, 0.3);
    EXPECT_EQ(assign->uvTransform.scaleU, 1.1);
    EXPECT_EQ(assign->uvTransform.scaleV, 1.2);
}

// The 3-arg overload (every EXISTING caller) still compiles and restores the
// identity transform -- see MaterialRepository::restoreAssignment's own comment.
TEST_F(MaterialTest, RestoreAssignmentThreeArgOverloadRestoresIdentityUvTransform) {
    const Id matId = materials->create("Wood", 0.5, 0.3, 0.1, 1.0);
    const EntityRef ref{EntityKind::Face, 1};
    EXPECT_TRUE(materials->restoreAssignment(ref, matId, 0));
    const auto* assign = materials->assignment(ref);
    ASSERT_NE(assign, nullptr);
    EXPECT_EQ(assign->uvTransform.offsetU, 0.0);
    EXPECT_EQ(assign->uvTransform.scaleU, 1.0);
}

TEST_F(MaterialTest, SetActiveNeverDispatchesMaterialsChanged) {
    const Id matId = materials->create("Wood", 0.5, 0.3, 0.1, 1.0);
    int changed = 0;
    kernel.dispatcher().subscribe<MaterialsChanged>(&changed, [&changed](const MaterialsChanged&) { ++changed; });

    materials->setActive(matId);
    EXPECT_EQ(materials->activeMaterialId(), matId);
    EXPECT_EQ(changed, 0);
}

// -- Texture fields --------

TEST_F(MaterialTest, SetTextureSetsFieldsAndFiresOnce) {
    const Id matId = materials->create("Wood", 0.5, 0.3, 0.1, 1.0);
    const std::string hash = assets->add("fake png bytes", "png");
    int changed = 0;
    kernel.dispatcher().subscribe<MaterialsChanged>(&changed, [&changed](const MaterialsChanged&) { ++changed; });

    EXPECT_TRUE(materials->setTexture(matId, hash, 2.0, 3.0));
    ASSERT_NE(materials->material(matId), nullptr);
    EXPECT_EQ(materials->material(matId)->assetHash, hash);
    EXPECT_EQ(materials->material(matId)->tileW, 2.0);
    EXPECT_EQ(materials->material(matId)->tileH, 3.0);
    EXPECT_EQ(changed, 1);
}

TEST_F(MaterialTest, SetTextureUnknownIdIsNoOp) {
    const std::string hash = assets->add("fake png bytes", "png");
    int changed = 0;
    kernel.dispatcher().subscribe<MaterialsChanged>(&changed, [&changed](const MaterialsChanged&) { ++changed; });

    EXPECT_FALSE(materials->setTexture(999, hash, 1.0, 1.0));
    EXPECT_EQ(changed, 0);
}

TEST_F(MaterialTest, SetTextureEmptyHashIsNoOp) {
    const Id matId = materials->create("Wood", 0.5, 0.3, 0.1, 1.0);
    int changed = 0;
    kernel.dispatcher().subscribe<MaterialsChanged>(&changed, [&changed](const MaterialsChanged&) { ++changed; });

    EXPECT_FALSE(materials->setTexture(matId, "", 1.0, 1.0));
    EXPECT_TRUE(materials->material(matId)->assetHash.empty());
    EXPECT_EQ(changed, 0);
}

TEST_F(MaterialTest, SetTextureWithIdenticalValuesIsNoOp) {
    const Id matId = materials->create("Wood", 0.5, 0.3, 0.1, 1.0);
    const std::string hash = assets->add("fake png bytes", "png");
    ASSERT_TRUE(materials->setTexture(matId, hash, 2.0, 3.0));

    int changed = 0;
    kernel.dispatcher().subscribe<MaterialsChanged>(&changed, [&changed](const MaterialsChanged&) { ++changed; });
    EXPECT_FALSE(materials->setTexture(matId, hash, 2.0, 3.0));
    EXPECT_EQ(changed, 0);
}

TEST_F(MaterialTest, ClearTextureResetsFieldsAndFiresOnce) {
    const Id matId = materials->create("Wood", 0.5, 0.3, 0.1, 1.0);
    const std::string hash = assets->add("fake png bytes", "png");
    ASSERT_TRUE(materials->setTexture(matId, hash, 2.0, 3.0));

    int changed = 0;
    kernel.dispatcher().subscribe<MaterialsChanged>(&changed, [&changed](const MaterialsChanged&) { ++changed; });
    EXPECT_TRUE(materials->clearTexture(matId));
    ASSERT_NE(materials->material(matId), nullptr);
    EXPECT_TRUE(materials->material(matId)->assetHash.empty());
    EXPECT_EQ(materials->material(matId)->tileW, 1.0);
    EXPECT_EQ(materials->material(matId)->tileH, 1.0);
    EXPECT_EQ(changed, 1);
}

TEST_F(MaterialTest, ClearTextureOnUntexturedMaterialIsNoOp) {
    const Id matId = materials->create("Wood", 0.5, 0.3, 0.1, 1.0);
    int changed = 0;
    kernel.dispatcher().subscribe<MaterialsChanged>(&changed, [&changed](const MaterialsChanged&) { ++changed; });

    EXPECT_FALSE(materials->clearTexture(matId));
    EXPECT_EQ(changed, 0);
}

TEST_F(MaterialTest, ClearTextureUnknownIdIsNoOp) {
    EXPECT_FALSE(materials->clearTexture(999));
}

TEST_F(MaterialTest, ClearForRestoreResetsMaterialsAssignmentsAndActiveId) {
    const Id matId = materials->create("Wood", 0.5, 0.3, 0.1, 1.0);
    materials->paint({EntityRef{EntityKind::Face, 1}}, matId);
    materials->setActive(matId);
    ASSERT_NE(materials->activeMaterialId(), 0u);

    materials->clearForRestore();

    EXPECT_TRUE(materials->materials().empty());
    EXPECT_TRUE(materials->assignments().empty());
    EXPECT_EQ(materials->activeMaterialId(), 0u);

    // nextId_ reset -- the very next create() reuses id 1.
    const Id freshId = materials->create("Fresh", 0, 0, 0, 1.0);
    EXPECT_EQ(freshId, 1u);
}

// -- Event-level: undo/redo, dirty tracking, aux-diff capture ---------------

TEST_F(MaterialTest, MaterialCreateRequestedUndoableRedoableAndMarksDirty) {
    ASSERT_FALSE(document->dirty());

    kernel.send(MaterialCreateRequested{"Wood", 0.5, 0.3, 0.1, 1.0});
    ASSERT_EQ(materials->materials().size(), 1u);
    EXPECT_TRUE(document->dirty());
    ASSERT_TRUE(undo->canUndo());

    kernel.send(UndoRequested{});
    EXPECT_TRUE(materials->materials().empty());
    EXPECT_FALSE(undo->canUndo());
    EXPECT_TRUE(undo->canRedo());

    kernel.send(RedoRequested{});
    ASSERT_EQ(materials->materials().size(), 1u);
    EXPECT_EQ(materials->materials()[0].name, "Wood");
}

TEST_F(MaterialTest, MaterialEditRequestedUndoableAndRedoable) {
    kernel.send(MaterialCreateRequested{"Wood", 0.5, 0.3, 0.1, 1.0});
    const Id id = materials->materials()[0].id;

    kernel.send(MaterialEditRequested{id, "Oak", 0.6, 0.4, 0.2, 0.8});
    ASSERT_NE(materials->material(id), nullptr);
    EXPECT_EQ(materials->material(id)->name, "Oak");

    kernel.send(UndoRequested{});
    ASSERT_NE(materials->material(id), nullptr);
    EXPECT_EQ(materials->material(id)->name, "Wood");

    kernel.send(RedoRequested{});
    ASSERT_NE(materials->material(id), nullptr);
    EXPECT_EQ(materials->material(id)->name, "Oak");
}

TEST_F(MaterialTest, PaintRequestedUndoableAndRedoableViaAuxDiff) {
    kernel.send(AddEdgeRequested{{0, 0, 0}, {1, 0, 0}});
    const Id vId = geometry->model().vertices().begin()->first;
    const EntityRef ref{EntityKind::Vertex, vId};

    kernel.send(MaterialCreateRequested{"Wood", 0.5, 0.3, 0.1, 1.0});
    const Id matId = materials->materials()[0].id;

    kernel.send(PaintRequested{{ref}, matId});
    ASSERT_NE(materials->assignment(ref), nullptr);
    EXPECT_EQ(materials->assignment(ref)->frontMaterialId, matId);

    kernel.send(UndoRequested{});  // undoes the paint only
    EXPECT_EQ(materials->assignment(ref), nullptr);
    ASSERT_EQ(materials->materials().size(), 1u);  // the create is a separate, earlier undo step

    kernel.send(RedoRequested{});
    ASSERT_NE(materials->assignment(ref), nullptr);
    EXPECT_EQ(materials->assignment(ref)->frontMaterialId, matId);
}

// SetUvTransformRequested is undoable/redoable via the same
// {materials, assignments} aux-diff path as PaintRequested (MaterialsSnapshot
// carries uvTransform too).
TEST_F(MaterialTest, SetUvTransformRequestedUndoableAndRedoableViaAuxDiff) {
    kernel.send(MaterialCreateRequested{"Wood", 0.5, 0.3, 0.1, 1.0});
    const Id matId = materials->materials()[0].id;
    const EntityRef ref{EntityKind::Face, 1};
    kernel.send(PaintRequested{{ref}, matId});

    kernel.send(SetUvTransformRequested{ref, UvTransform{0.5, -0.25, 0.3, 1.5, 0.8}});
    ASSERT_NE(materials->assignment(ref), nullptr);
    EXPECT_EQ(materials->assignment(ref)->uvTransform.offsetU, 0.5);

    kernel.send(UndoRequested{});  // undoes the UV-transform set only
    ASSERT_NE(materials->assignment(ref), nullptr);
    EXPECT_EQ(materials->assignment(ref)->uvTransform.offsetU, 0.0);  // back to identity
    EXPECT_EQ(materials->assignment(ref)->frontMaterialId, matId);    // the paint itself is untouched

    kernel.send(RedoRequested{});
    ASSERT_NE(materials->assignment(ref), nullptr);
    EXPECT_EQ(materials->assignment(ref)->uvTransform.offsetU, 0.5);
    EXPECT_EQ(materials->assignment(ref)->uvTransform.scaleU, 1.5);
}

// Undo/redo of a texture-set reverts only the Material's own
// {assetHash, tileW, tileH} fields -- the AssetRepository blob is untouched
// either way (additive-only, excluded from the undo aux-diff set).
TEST_F(MaterialTest, MaterialSetTextureRequestedUndoableRedoableAndBlobPersistsAfterUndo) {
    ASSERT_TRUE(tempDir.isValid());
    kernel.send(MaterialCreateRequested{"Wood", 0.5, 0.3, 0.1, 1.0});
    const Id matId = materials->materials()[0].id;

    const std::string imagePath = tempFilePath("texture.png");
    QFile file(QString::fromStdString(imagePath));
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    file.write("fake png bytes");
    file.close();

    kernel.send(MaterialSetTextureRequested{matId, imagePath, 2.0, 3.0});
    ASSERT_NE(materials->material(matId), nullptr);
    const std::string hash = materials->material(matId)->assetHash;
    EXPECT_FALSE(hash.empty());
    EXPECT_EQ(materials->material(matId)->tileW, 2.0);
    EXPECT_EQ(materials->material(matId)->tileH, 3.0);
    ASSERT_NE(assets->get(hash), nullptr);

    kernel.send(UndoRequested{});  // undoes the texture-set only
    ASSERT_NE(materials->material(matId), nullptr);
    EXPECT_TRUE(materials->material(matId)->assetHash.empty());
    EXPECT_EQ(materials->material(matId)->tileW, 1.0);
    EXPECT_EQ(materials->material(matId)->tileH, 1.0);
    EXPECT_NE(assets->get(hash), nullptr);  // blob persists -- not undo-captured

    kernel.send(RedoRequested{});
    ASSERT_NE(materials->material(matId), nullptr);
    EXPECT_EQ(materials->material(matId)->assetHash, hash);
    EXPECT_EQ(materials->material(matId)->tileW, 2.0);
    EXPECT_EQ(materials->material(matId)->tileH, 3.0);
}

TEST_F(MaterialTest, MaterialSetTextureRequestedWithEmptyPathClearsExistingTexture) {
    ASSERT_TRUE(tempDir.isValid());
    kernel.send(MaterialCreateRequested{"Wood", 0.5, 0.3, 0.1, 1.0});
    const Id matId = materials->materials()[0].id;

    const std::string imagePath = tempFilePath("texture.png");
    QFile file(QString::fromStdString(imagePath));
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    file.write("fake png bytes");
    file.close();

    kernel.send(MaterialSetTextureRequested{matId, imagePath, 1.0, 1.0});
    ASSERT_FALSE(materials->material(matId)->assetHash.empty());

    kernel.send(MaterialSetTextureRequested{matId, "", 1.0, 1.0});
    EXPECT_TRUE(materials->material(matId)->assetHash.empty());
}

// An unsupported extension rejects the whole call (no mutation) and reports
// events::DocumentIoFailed, same failure-reporting convention as
// document_commands.cpp's own reportIoFailure.
TEST_F(MaterialTest, MaterialSetTextureRequestedRejectsUnsupportedExtension) {
    ASSERT_TRUE(tempDir.isValid());
    kernel.send(MaterialCreateRequested{"Wood", 0.5, 0.3, 0.1, 1.0});
    const Id matId = materials->materials()[0].id;

    const std::string badPath = tempFilePath("texture.bmp");
    QFile file(QString::fromStdString(badPath));
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    file.write("not really a bmp");
    file.close();

    int failed = 0;
    kernel.dispatcher().subscribe<DocumentIoFailed>(&failed, [&failed](const DocumentIoFailed&) { ++failed; });

    kernel.send(MaterialSetTextureRequested{matId, badPath, 1.0, 1.0});
    EXPECT_TRUE(materials->material(matId)->assetHash.empty());
    EXPECT_EQ(failed, 1);
}

// SetActiveMaterialRequested is deliberately not undo-wrapped -- proven by
// exactly one more UndoRequested (undoing only the original create) emptying
// the list; a wrapped command would leave the material still present.
TEST_F(MaterialTest, SetActiveMaterialRequestedIsNotUndoableNorDirty) {
    kernel.send(MaterialCreateRequested{"Wood", 0.5, 0.3, 0.1, 1.0});
    const Id matId = materials->materials()[0].id;
    const std::string path = tempFilePath("model.plr");
    kernel.send(SaveDocumentRequested{path});
    ASSERT_FALSE(document->dirty());

    kernel.send(SetActiveMaterialRequested{matId});
    EXPECT_EQ(materials->activeMaterialId(), matId);
    EXPECT_FALSE(document->dirty());  // not dirty-worthy

    kernel.send(UndoRequested{});
    EXPECT_TRUE(materials->materials().empty());
    EXPECT_FALSE(undo->canUndo());
}

// "Survives nothing" -- clearForRestore() (as New/Open do) wipes the active
// selection too, since it is excluded from the undo aux-diff set entirely
// (there is nothing else that would ever reset it).
TEST_F(MaterialTest, ActiveMaterialClearedByNewDocument) {
    kernel.send(MaterialCreateRequested{"Wood", 0.5, 0.3, 0.1, 1.0});
    const Id matId = materials->materials()[0].id;
    kernel.send(SetActiveMaterialRequested{matId});
    ASSERT_EQ(materials->activeMaterialId(), matId);

    kernel.send(NewDocumentRequested{});
    EXPECT_EQ(materials->activeMaterialId(), 0u);
}

// -- .plr round-trip via real New/Open/SaveDocumentCommand events -----------

TEST_F(MaterialTest, SaveOpenRoundTripsMaterialsWithInstanceAndStaleAssignment) {
    ASSERT_TRUE(tempDir.isValid());

    // Instance target: group a rectangle into a component instance.
    kernel.send(AddRectangleRequested{{0, 0, 0}, {2, 2, 0}});
    const Id faceId = geometry->model().faces().begin()->first;
    kernel.send(SelectRequested{SelectMode::Replace, EntityRef{EntityKind::Face, faceId}, SelectExpand::None});
    kernel.send(GroupCreateRequested{false, "MyGroup"});
    ASSERT_EQ(geometry->scene().root().children.size(), 1u);
    const Id instanceId = geometry->scene().root().children[0].id;

    // Stale target: a wire edge, painted then removed.
    kernel.send(AddEdgeRequested{{10, 0, 0}, {11, 0, 0}});
    ASSERT_EQ(geometry->model().edges().size(), 1u);
    const Id wireEdgeId = geometry->model().edges().begin()->first;

    kernel.send(MaterialCreateRequested{"Wood", 0.5, 0.3, 0.1, 1.0});
    const Id woodId = materials->materials()[0].id;

    kernel.send(PaintRequested{{EntityRef{EntityKind::Instance, instanceId}}, woodId});
    kernel.send(PaintRequested{{EntityRef{EntityKind::Edge, wireEdgeId}}, woodId});
    ASSERT_EQ(materials->assignments().size(), 2u);

    kernel.send(RemoveEdgeRequested{wireEdgeId});  // the edge is gone -- its assignment is now stale
    ASSERT_TRUE(geometry->model().edges().empty());
    ASSERT_EQ(materials->assignments().size(), 2u);  // MaterialRepository never prunes -- stale entry tolerated live

    const std::string path = tempFilePath("model.plr");
    kernel.send(SaveDocumentRequested{path});
    ASSERT_FALSE(document->dirty());

    kernel.send(NewDocumentRequested{});
    EXPECT_TRUE(materials->materials().empty());
    EXPECT_TRUE(materials->assignments().empty());

    kernel.send(OpenDocumentRequested{path});
    ASSERT_EQ(materials->materials().size(), 1u);
    EXPECT_EQ(materials->materials()[0].name, "Wood");
    const Id reloadedWoodId = materials->materials()[0].id;
    ASSERT_EQ(materials->assignments().size(), 2u);
    const auto* instAssign = materials->assignment(EntityRef{EntityKind::Instance, instanceId});
    ASSERT_NE(instAssign, nullptr);
    EXPECT_EQ(instAssign->frontMaterialId, reloadedWoodId);
    const auto* staleAssign = materials->assignment(EntityRef{EntityKind::Edge, wireEdgeId});
    ASSERT_NE(staleAssign, nullptr);  // the stale entry round-tripped verbatim
    EXPECT_EQ(staleAssign->frontMaterialId, reloadedWoodId);
}

}  // namespace

// -- Free-standing .plr schema/compatibility/error tests -- no MaterialTest
// fixture needed, drive io::writeDocument/readDocument directly.
namespace {

using plnr::io::CameraState;
using plnr::io::DocumentMeta;
using plnr::io::ReadResult;

// Exercises the front+back pair directly via the Restore API -- paint() only
// ever writes the front slot, so this is the one place the back slot's
// round-trip gets checked.
TEST(MaterialPlrSchemaTest, WriteReadWriteRoundTripsMaterialsWithFrontAndBackAssignment) {
    Kernel kernel;
    kernel.registerAgent(std::make_shared<GeometryApi>());
    kernel.registerAgent(std::make_shared<TagStore>());
    kernel.registerAgent(std::make_shared<GuideStore>());
    kernel.registerAgent(std::make_shared<AnnotationStore>());
    kernel.registerAgent(std::make_shared<SectionStore>());
    kernel.registerAgent(std::make_shared<AxesStore>());
    kernel.registerAgent(std::make_shared<MaterialRepository>());
    kernel.registerAgent(std::make_shared<StyleStore>());
    kernel.registerAgent(std::make_shared<ShadowStore>());
    kernel.registerAgent(std::make_shared<FogStore>());
    auto geometry = kernel.agentAs<GeometryApi>(kGeometryApiName);
    auto tags = kernel.agentAs<TagStore>(kTagStoreName);
    auto guides = kernel.agentAs<GuideStore>(kGuideStoreName);
    auto annotations = kernel.agentAs<AnnotationStore>(kAnnotationStoreName);
    auto sections = kernel.agentAs<SectionStore>(kSectionStoreName);
    auto axes = kernel.agentAs<AxesStore>(kAxesStoreName);
    auto materials = kernel.agentAs<MaterialRepository>(kMaterialRepositoryName);
    auto style = kernel.agentAs<StyleStore>(kStyleStoreName);
    auto shadow = kernel.agentAs<ShadowStore>(kShadowStoreName);
    auto fog = kernel.agentAs<FogStore>(kFogStoreName);

    ASSERT_TRUE(geometry->addRectangle({0, 0, 0}, {2, 2, 0}));
    const Id faceId = geometry->model().faces().begin()->first;

    const Id woodId = materials->create("Wood", 0.5, 0.3, 0.1, 1.0);
    const Id glassId = materials->create("Glass", 0.8, 0.9, 1.0, 0.4);
    ASSERT_TRUE(materials->restoreAssignment(EntityRef{EntityKind::Face, faceId}, woodId, glassId));

    const CameraState camera{};
    const DocumentMeta meta{QStringLiteral("test"), QStringLiteral("t"), QStringLiteral("in")};
    const QByteArray bytesA =
        plnr::io::writeDocument(*geometry, *tags, *guides, *annotations, *sections, *axes, *materials, *style,
                                *shadow, *fog, camera, meta)
            .toJson(QJsonDocument::Indented);

    GeometryApi geometry2;
    TagStore tags2;
    GuideStore guides2;
    AnnotationStore annotations2;
    SectionStore sections2;
    AxesStore axes2;
    MaterialRepository materials2;
    AssetRepository assets2;
    StyleStore style2;
    ShadowStore shadow2;
    FogStore fog2;
    CameraState cameraOut;
    DocumentMeta metaOut;
    const ReadResult result = plnr::io::readDocument(bytesA, geometry2, tags2, guides2, annotations2, sections2, axes2,
                                                      materials2, assets2, style2, shadow2, fog2, &cameraOut, &metaOut);
    ASSERT_TRUE(result.ok) << result.error.toStdString();

    ASSERT_EQ(materials2.materials().size(), 2u);
    const auto* assign = materials2.assignment(EntityRef{EntityKind::Face, faceId});
    ASSERT_NE(assign, nullptr);
    EXPECT_EQ(assign->frontMaterialId, woodId);
    EXPECT_EQ(assign->backMaterialId, glassId);

    const QByteArray bytesB =
        plnr::io::writeDocument(geometry2, tags2, guides2, annotations2, sections2, axes2, materials2, style2,
                                shadow2, fog2, cameraOut, metaOut)
            .toJson(QJsonDocument::Indented);
    EXPECT_EQ(bytesA, bytesB);
}

// A non-identity UV transform round-trips through the optional 5th
// materialAssignments element, byte-stable both ways (write -> read -> write
// again produces the same bytes).
TEST(MaterialPlrSchemaTest, WriteReadWriteRoundTripsNonIdentityUvTransform) {
    Kernel kernel;
    kernel.registerAgent(std::make_shared<GeometryApi>());
    kernel.registerAgent(std::make_shared<TagStore>());
    kernel.registerAgent(std::make_shared<GuideStore>());
    kernel.registerAgent(std::make_shared<AnnotationStore>());
    kernel.registerAgent(std::make_shared<SectionStore>());
    kernel.registerAgent(std::make_shared<AxesStore>());
    kernel.registerAgent(std::make_shared<MaterialRepository>());
    kernel.registerAgent(std::make_shared<StyleStore>());
    kernel.registerAgent(std::make_shared<ShadowStore>());
    kernel.registerAgent(std::make_shared<FogStore>());
    auto geometry = kernel.agentAs<GeometryApi>(kGeometryApiName);
    auto tags = kernel.agentAs<TagStore>(kTagStoreName);
    auto guides = kernel.agentAs<GuideStore>(kGuideStoreName);
    auto annotations = kernel.agentAs<AnnotationStore>(kAnnotationStoreName);
    auto sections = kernel.agentAs<SectionStore>(kSectionStoreName);
    auto axes = kernel.agentAs<AxesStore>(kAxesStoreName);
    auto materials = kernel.agentAs<MaterialRepository>(kMaterialRepositoryName);
    auto style = kernel.agentAs<StyleStore>(kStyleStoreName);
    auto shadow = kernel.agentAs<ShadowStore>(kShadowStoreName);
    auto fog = kernel.agentAs<FogStore>(kFogStoreName);

    ASSERT_TRUE(geometry->addRectangle({0, 0, 0}, {2, 2, 0}));
    const Id faceId = geometry->model().faces().begin()->first;

    const Id woodId = materials->create("Wood", 0.5, 0.3, 0.1, 1.0);
    const UvTransform t{0.5, -0.25, 0.3, 1.5, 0.8};
    ASSERT_TRUE(materials->restoreAssignment(EntityRef{EntityKind::Face, faceId}, woodId, 0, t));

    const CameraState camera{};
    const DocumentMeta meta{QStringLiteral("test"), QStringLiteral("t"), QStringLiteral("in")};
    const QByteArray bytesA =
        plnr::io::writeDocument(*geometry, *tags, *guides, *annotations, *sections, *axes, *materials, *style,
                                *shadow, *fog, camera, meta)
            .toJson(QJsonDocument::Indented);
    // Sanity: the 5th element's short-form keys actually made it into the
    // written JSON (a substring check is enough here -- the exact schema is
    // what the round-trip assertion below actually proves).
    EXPECT_TRUE(bytesA.contains("\"du\""));
    EXPECT_TRUE(bytesA.contains("\"rot\""));

    GeometryApi geometry2;
    TagStore tags2;
    GuideStore guides2;
    AnnotationStore annotations2;
    SectionStore sections2;
    AxesStore axes2;
    MaterialRepository materials2;
    AssetRepository assets2;
    StyleStore style2;
    ShadowStore shadow2;
    FogStore fog2;
    CameraState cameraOut;
    DocumentMeta metaOut;
    const ReadResult result = plnr::io::readDocument(bytesA, geometry2, tags2, guides2, annotations2, sections2, axes2,
                                                      materials2, assets2, style2, shadow2, fog2, &cameraOut, &metaOut);
    ASSERT_TRUE(result.ok) << result.error.toStdString();

    const auto* assign = materials2.assignment(EntityRef{EntityKind::Face, faceId});
    ASSERT_NE(assign, nullptr);
    EXPECT_EQ(assign->uvTransform.offsetU, 0.5);
    EXPECT_EQ(assign->uvTransform.offsetV, -0.25);
    EXPECT_EQ(assign->uvTransform.rotationRad, 0.3);
    EXPECT_EQ(assign->uvTransform.scaleU, 1.5);
    EXPECT_EQ(assign->uvTransform.scaleV, 0.8);

    const QByteArray bytesB =
        plnr::io::writeDocument(geometry2, tags2, guides2, annotations2, sections2, axes2, materials2, style2,
                                shadow2, fog2, cameraOut, metaOut)
            .toJson(QJsonDocument::Indented);
    EXPECT_EQ(bytesA, bytesB);
}

// The compatibility contract's other half: an identity UV transform writes
// the same 4-element row the 3-arg restoreAssignment overload would --
// byte-identical, no 5th element at all.
TEST(MaterialPlrSchemaTest, IdentityUvTransformOmittedProducesByteIdenticalOutput) {
    Kernel kernel;
    kernel.registerAgent(std::make_shared<GeometryApi>());
    kernel.registerAgent(std::make_shared<TagStore>());
    kernel.registerAgent(std::make_shared<GuideStore>());
    kernel.registerAgent(std::make_shared<AnnotationStore>());
    kernel.registerAgent(std::make_shared<SectionStore>());
    kernel.registerAgent(std::make_shared<AxesStore>());
    kernel.registerAgent(std::make_shared<MaterialRepository>());
    kernel.registerAgent(std::make_shared<StyleStore>());
    kernel.registerAgent(std::make_shared<ShadowStore>());
    kernel.registerAgent(std::make_shared<FogStore>());
    auto geometry = kernel.agentAs<GeometryApi>(kGeometryApiName);
    auto tags = kernel.agentAs<TagStore>(kTagStoreName);
    auto guides = kernel.agentAs<GuideStore>(kGuideStoreName);
    auto annotations = kernel.agentAs<AnnotationStore>(kAnnotationStoreName);
    auto sections = kernel.agentAs<SectionStore>(kSectionStoreName);
    auto axes = kernel.agentAs<AxesStore>(kAxesStoreName);
    auto materials = kernel.agentAs<MaterialRepository>(kMaterialRepositoryName);
    auto style = kernel.agentAs<StyleStore>(kStyleStoreName);
    auto shadow = kernel.agentAs<ShadowStore>(kShadowStoreName);
    auto fog = kernel.agentAs<FogStore>(kFogStoreName);

    ASSERT_TRUE(geometry->addRectangle({0, 0, 0}, {2, 2, 0}));
    const Id faceId = geometry->model().faces().begin()->first;
    const Id woodId = materials->create("Wood", 0.5, 0.3, 0.1, 1.0);
    ASSERT_TRUE(materials->restoreAssignment(EntityRef{EntityKind::Face, faceId}, woodId, 0, UvTransform{}));

    const CameraState camera{};
    const DocumentMeta meta{QStringLiteral("test"), QStringLiteral("t"), QStringLiteral("in")};
    const QByteArray bytesWithExplicitIdentity =
        plnr::io::writeDocument(*geometry, *tags, *guides, *annotations, *sections, *axes, *materials, *style,
                                *shadow, *fog, camera, meta)
            .toJson(QJsonDocument::Indented);
    EXPECT_FALSE(bytesWithExplicitIdentity.contains("\"du\""));

    // Same document, but the assignment restored via the pre-existing 3-arg
    // overload instead -- must produce byte-IDENTICAL output.
    MaterialRepository materials3;
    materials3.restoreMaterial(Material{woodId, "Wood", 0.5, 0.3, 0.1, 1.0});
    ASSERT_TRUE(materials3.restoreAssignment(EntityRef{EntityKind::Face, faceId}, woodId, 0));
    StyleStore style3;    // untouched, same all-default state as `style` above
    ShadowStore shadow3;  // untouched, same all-default state as `shadow` above
    FogStore fog3;        // untouched, same all-default state as `fog` above
    const QByteArray bytesViaThreeArgOverload =
        plnr::io::writeDocument(*geometry, *tags, *guides, *annotations, *sections, *axes, materials3, style3, shadow3,
                                fog3, camera, meta)
            .toJson(QJsonDocument::Indented);
    EXPECT_EQ(bytesWithExplicitIdentity, bytesViaThreeArgOverload);
}

// A dangling materialAssignments reference must reject the whole file,
// leaving every agent argument untouched -- same all-or-nothing contract
// tagAssignments' own tagId reference has.
TEST(MaterialPlrErrorTest, DanglingMaterialAssignmentReferenceRejectsAllOrNothing) {
    Kernel kernel;
    kernel.registerAgent(std::make_shared<GeometryApi>());
    kernel.registerAgent(std::make_shared<TagStore>());
    kernel.registerAgent(std::make_shared<GuideStore>());
    kernel.registerAgent(std::make_shared<AnnotationStore>());
    kernel.registerAgent(std::make_shared<SectionStore>());
    kernel.registerAgent(std::make_shared<AxesStore>());
    kernel.registerAgent(std::make_shared<MaterialRepository>());
    kernel.registerAgent(std::make_shared<StyleStore>());
    kernel.registerAgent(std::make_shared<ShadowStore>());
    kernel.registerAgent(std::make_shared<FogStore>());
    auto geometry = kernel.agentAs<GeometryApi>(kGeometryApiName);
    auto tags = kernel.agentAs<TagStore>(kTagStoreName);
    auto guides = kernel.agentAs<GuideStore>(kGuideStoreName);
    auto annotations = kernel.agentAs<AnnotationStore>(kAnnotationStoreName);
    auto sections = kernel.agentAs<SectionStore>(kSectionStoreName);
    auto axes = kernel.agentAs<AxesStore>(kAxesStoreName);
    auto materials = kernel.agentAs<MaterialRepository>(kMaterialRepositoryName);
    auto style = kernel.agentAs<StyleStore>(kStyleStoreName);
    auto shadow = kernel.agentAs<ShadowStore>(kShadowStoreName);
    auto fog = kernel.agentAs<FogStore>(kFogStoreName);

    ASSERT_TRUE(geometry->addRectangle({0, 0, 0}, {1, 1, 0}));
    const Id faceId = geometry->model().faces().begin()->first;
    materials->create("Wood", 0.5, 0.3, 0.1, 1.0);

    const CameraState camera{};
    const DocumentMeta meta{QStringLiteral("test"), QStringLiteral("t"), QStringLiteral("in")};
    QJsonObject doc =
        plnr::io::writeDocument(*geometry, *tags, *guides, *annotations, *sections, *axes, *materials, *style,
                                *shadow, *fog, camera, meta)
            .object();

    // Hand-craft a dangling materialAssignments row -- front names a
    // material id (999999) that doesn't exist.
    QJsonArray assignments;
    assignments.append(QJsonArray{QStringLiteral("face"), static_cast<double>(faceId), 999999.0, 0.0});
    doc["materialAssignments"] = assignments;
    const QByteArray bytes = QJsonDocument(doc).toJson(QJsonDocument::Indented);

    GeometryApi geometry2;
    TagStore tags2;
    GuideStore guides2;
    AnnotationStore annotations2;
    SectionStore sections2;
    AxesStore axes2;
    MaterialRepository materials2;
    AssetRepository assets2;
    StyleStore style2;
    ShadowStore shadow2;
    FogStore fog2;
    // restoreMaterial(), not create() -- materials2 is never registered on a
    // kernel here, and create() would throw (Agent::send() requires a
    // registered context()).
    materials2.restoreMaterial(Material{1, "PreexistingMarkerMaterial", 1, 1, 1, 1.0});
    CameraState cameraOut;
    DocumentMeta metaOut;
    const ReadResult result = plnr::io::readDocument(bytes, geometry2, tags2, guides2, annotations2, sections2, axes2,
                                                      materials2, assets2, style2, shadow2, fog2, &cameraOut, &metaOut);

    EXPECT_FALSE(result.ok);
    EXPECT_FALSE(result.error.isEmpty());
    ASSERT_EQ(materials2.materials().size(), 1u);  // untouched -- all-or-nothing
    EXPECT_EQ(materials2.materials()[0].name, "PreexistingMarkerMaterial");
}

}  // namespace
