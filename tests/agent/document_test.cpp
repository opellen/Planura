#include "agent/command/document_commands.h"

#include <memory>
#include <string>
#include <vector>

#include <QDir>
#include <QString>
#include <QTemporaryDir>

#include <ordo/core/app_kernel.h>

#include "agent/command/annotation_commands.h"
#include "agent/annotation_store.h"
#include "agent/asset_repository.h"
#include "agent/axes_store.h"
#include "agent/camera_store.h"
#include "agent/document_store.h"
#include "agent/edit_context_store.h"
#include "agent/events.h"
#include "agent/fog_store.h"
#include "agent/command/geometry_commands.h"
#include "agent/geometry_api.h"
#include "agent/command/guide_commands.h"
#include "agent/guide_store.h"
#include "agent/material_repository.h"
#include "agent/section_store.h"
#include "agent/command/selection_commands.h"
#include "agent/selection_store.h"
#include "agent/shadow_store.h"
#include "agent/style_store.h"
#include "agent/command/tag_commands.h"
#include "agent/tag_store.h"

#include <geo/entity.h>
#include <geo/vec3.h>

#include <gtest/gtest.h>

// Fresh AppKernel per test with just the Agents/Commands needed, real
// events driving mutations, and a bare dispatcher subscription (not a
// Presenter) to observe Facts no production Command consumes.
namespace {

using ordo::core::AppKernel;
using plnr::agent::AddEdgeCommand;
using plnr::agent::AddGuidePointCommand;
using plnr::agent::AnnotationStore;
using plnr::agent::AssetRepository;
using plnr::agent::AxesStore;
using plnr::agent::CameraState;
using plnr::agent::CameraStore;
using plnr::agent::CameraSyncCommand;
using plnr::agent::DocumentStore;
using plnr::agent::EditContextStore;
using plnr::agent::GeometryChangedCommand;
using plnr::agent::GeometryApi;
using plnr::agent::GuideStore;
using plnr::agent::kAnnotationStoreName;
using plnr::agent::kAxesStoreName;
using plnr::agent::kCameraStoreName;
using plnr::agent::kDocumentStoreName;
using plnr::agent::kEditContextStoreName;
using plnr::agent::kGeometryApiName;
using plnr::agent::kGuideStoreName;
using plnr::agent::kMaterialRepositoryName;
using plnr::agent::kSectionStoreName;
using plnr::agent::kSelectionStoreName;
using plnr::agent::kTagStoreName;
using plnr::agent::MarkDirtyCommand;
using plnr::agent::MaterialRepository;
using plnr::agent::NewDocumentCommand;
using plnr::agent::OpenDocumentCommand;
using plnr::agent::SaveDocumentCommand;
using plnr::agent::SectionStore;
using plnr::agent::SelectCommand;
using plnr::agent::SelectionStore;
using plnr::agent::SetHiddenCommand;
using plnr::agent::TagCreateCommand;
using plnr::agent::TagStore;
using plnr::events::AddEdgeRequested;
using plnr::events::AddGuidePointRequested;
using plnr::events::AnnotationsChanged;
using plnr::events::AxesChanged;
using plnr::events::CameraChanged;
using plnr::events::CameraNavigated;
using plnr::events::DocumentIoFailed;
using plnr::events::EntityRef;
using plnr::events::GeometryChanged;
using plnr::events::GuidesChanged;
using plnr::events::NewDocumentRequested;
using plnr::events::OpenDocumentRequested;
using plnr::events::SaveDocumentRequested;
using plnr::events::SectionsChanged;
using plnr::events::SelectExpand;
using plnr::events::SelectMode;
using plnr::events::SelectRequested;
using plnr::events::SetHiddenRequested;
using plnr::events::TagCreateRequested;
using plnr::events::TagsChanged;
using plnr::geo::EntityKind;
using plnr::geo::Id;

class DocumentCommandTest : public ::testing::Test {
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
        kernel.registerAgent(std::make_shared<plnr::agent::StyleStore>());
        kernel.registerAgent(std::make_shared<plnr::agent::ShadowStore>());
        kernel.registerAgent(std::make_shared<plnr::agent::FogStore>());
        kernel.registerAgent(std::make_shared<SelectionStore>());
        kernel.registerAgent(std::make_shared<EditContextStore>());
        kernel.registerAgent(std::make_shared<CameraStore>());
        kernel.registerAgent(std::make_shared<DocumentStore>());

        kernel.registerCommand<AddEdgeRequested, AddEdgeCommand>();
        kernel.registerCommand<TagCreateRequested, TagCreateCommand>();
        kernel.registerCommand<AddGuidePointRequested, AddGuidePointCommand>();
        kernel.registerCommand<SelectRequested, SelectCommand>();
        kernel.registerCommand<SetHiddenRequested, SetHiddenCommand>();
        // GeometryChanged is claimed by GeometryChangedCommand -- see main.cpp's
        // claim-map comment for why the 5 commands below are separate
        // MarkDirtyCommand<EventT> instantiations instead.
        kernel.registerCommand<GeometryChanged, GeometryChangedCommand>();
        kernel.registerCommand<TagsChanged, MarkDirtyCommand<TagsChanged>>();
        kernel.registerCommand<GuidesChanged, MarkDirtyCommand<GuidesChanged>>();
        kernel.registerCommand<AnnotationsChanged, MarkDirtyCommand<AnnotationsChanged>>();
        kernel.registerCommand<SectionsChanged, MarkDirtyCommand<SectionsChanged>>();
        kernel.registerCommand<AxesChanged, MarkDirtyCommand<AxesChanged>>();
        kernel.registerCommand<CameraNavigated, CameraSyncCommand>();
        kernel.registerCommand<NewDocumentRequested, NewDocumentCommand>();
        kernel.registerCommand<OpenDocumentRequested, OpenDocumentCommand>();
        kernel.registerCommand<SaveDocumentRequested, SaveDocumentCommand>();

        kernel.dispatcher().subscribe<CameraChanged>(&cameraChangedCount,
                                                       [this](const CameraChanged&) { ++cameraChangedCount; });
        kernel.dispatcher().subscribe<DocumentIoFailed>(
            &ioFailedMessages, [this](const DocumentIoFailed& e) { ioFailedMessages.push_back(e.message); });

        geometry = kernel.agentAs<GeometryApi>(kGeometryApiName);
        tags = kernel.agentAs<TagStore>(kTagStoreName);
        guides = kernel.agentAs<GuideStore>(kGuideStoreName);
        selection = kernel.agentAs<SelectionStore>(kSelectionStoreName);
        camera = kernel.agentAs<CameraStore>(kCameraStoreName);
        document = kernel.agentAs<DocumentStore>(kDocumentStoreName);
        ASSERT_NE(geometry, nullptr);
        ASSERT_NE(tags, nullptr);
        ASSERT_NE(guides, nullptr);
        ASSERT_NE(selection, nullptr);
        ASSERT_NE(camera, nullptr);
        ASSERT_NE(document, nullptr);
    }

    // Path to a not-yet-existing file inside a fresh temp directory --
    // QTemporaryDir itself is kept alive on the fixture (RAII cleanup on
    // destruction) so the path stays valid for the whole test body.
    std::string tempFilePath(const char* name) const {
        return QDir(tempDir.path()).filePath(QString::fromUtf8(name)).toStdString();
    }

    AppKernel kernel;
    QTemporaryDir tempDir;
    std::shared_ptr<GeometryApi> geometry;
    std::shared_ptr<TagStore> tags;
    std::shared_ptr<GuideStore> guides;
    std::shared_ptr<SelectionStore> selection;
    std::shared_ptr<CameraStore> camera;
    std::shared_ptr<DocumentStore> document;
    int cameraChangedCount = 0;
    std::vector<std::string> ioFailedMessages;
};

TEST_F(DocumentCommandTest, SaveNewOpenRoundTripsEntitiesTagsGuidesHiddenAndCamera) {
    ASSERT_TRUE(tempDir.isValid());

    // Build a small document via real events.
    kernel.send(AddEdgeRequested{{0, 0, 0}, {4, 0, 0}});
    kernel.send(AddEdgeRequested{{4, 0, 0}, {4, 3, 0}});
    kernel.send(AddEdgeRequested{{4, 3, 0}, {0, 3, 0}});
    kernel.send(AddEdgeRequested{{0, 3, 0}, {0, 0, 0}});  // closes the loop -- 1 face
    kernel.send(TagCreateRequested{"Wood"});
    kernel.send(AddGuidePointRequested{{1, 1, 1}});

    ASSERT_EQ(geometry->model().vertices().size(), 4u);
    ASSERT_EQ(geometry->model().edges().size(), 4u);
    ASSERT_EQ(geometry->model().faces().size(), 1u);
    const Id vertexId = geometry->model().vertices().begin()->first;
    kernel.send(SetHiddenRequested{{EntityRef{EntityKind::Vertex, vertexId}}, true});
    ASSERT_TRUE(geometry->isHidden(EntityRef{EntityKind::Vertex, vertexId}));

    kernel.send(CameraNavigated{{5, 6, 7}, 12.0, 34.0, 56.0, 40.0});
    ASSERT_EQ(camera->state().target.x, 5.0);

    const std::vector<Id> savedVertexIds = [&] {
        std::vector<Id> ids;
        for (const auto& kv : geometry->model().vertices()) ids.push_back(kv.first);
        return ids;
    }();
    const std::size_t savedTagCount = tags->tags().size();
    const std::size_t savedGuideCount = guides->guides().size();

    const std::string path = tempFilePath("model.plr");
    kernel.send(SaveDocumentRequested{path});
    EXPECT_TRUE(ioFailedMessages.empty());
    EXPECT_EQ(document->filePath(), path);
    EXPECT_FALSE(document->dirty());

    kernel.send(NewDocumentRequested{});
    EXPECT_TRUE(geometry->model().vertices().empty());  // root mesh empty
    ASSERT_EQ(tags->tags().size(), 1u);                  // only Untagged survives
    EXPECT_EQ(tags->tags()[0].name, "Untagged");
    EXPECT_TRUE(document->filePath().empty());  // back to "Untitled"
    EXPECT_FALSE(document->dirty());

    kernel.send(OpenDocumentRequested{path});
    EXPECT_TRUE(ioFailedMessages.empty());
    EXPECT_EQ(document->filePath(), path);
    EXPECT_FALSE(document->dirty());

    // Entities round-tripped by id.
    ASSERT_EQ(geometry->model().vertices().size(), savedVertexIds.size());
    for (Id id : savedVertexIds) {
        EXPECT_NE(geometry->model().vertex(id), nullptr) << "vertex id " << id << " did not round-trip";
    }
    EXPECT_EQ(geometry->model().faces().size(), 1u);

    // Tags round-tripped by id/count.
    EXPECT_EQ(tags->tags().size(), savedTagCount);

    // Guides round-tripped by count.
    EXPECT_EQ(guides->guides().size(), savedGuideCount);

    // Hidden set round-tripped by id.
    EXPECT_TRUE(geometry->isHidden(EntityRef{EntityKind::Vertex, vertexId}));

    // Camera round-tripped.
    EXPECT_EQ(camera->state().target.x, 5.0);
    EXPECT_EQ(camera->state().azimuthDeg, 12.0);
    EXPECT_EQ(camera->state().elevationDeg, 34.0);
    EXPECT_EQ(camera->state().distance, 56.0);
    EXPECT_EQ(camera->state().fovYDeg, 40.0);
}

TEST_F(DocumentCommandTest, DirtyFlagTransitionsAcrossGeometryTagsAndSave) {
    ASSERT_TRUE(tempDir.isValid());
    const std::string path = tempFilePath("model.plr");

    // Clean after a successful save of an empty document.
    kernel.send(SaveDocumentRequested{path});
    ASSERT_FALSE(document->dirty());

    // GeometryChanged-driven mutation -> dirty.
    kernel.send(AddEdgeRequested{{0, 0, 0}, {1, 0, 0}});
    EXPECT_TRUE(document->dirty());

    // Save -> clean.
    kernel.send(SaveDocumentRequested{path});
    EXPECT_FALSE(document->dirty());

    // TagsChanged -> dirty.
    kernel.send(TagCreateRequested{"Metal"});
    EXPECT_TRUE(document->dirty());
}

TEST_F(DocumentCommandTest, SelectionChangedDoesNotMarkDirty) {
    kernel.send(AddEdgeRequested{{0, 0, 0}, {1, 0, 0}});
    ASSERT_TRUE(document->dirty());  // from the geometry mutation above

    const std::string path = tempFilePath("model.plr");
    kernel.send(SaveDocumentRequested{path});
    ASSERT_FALSE(document->dirty());

    const Id vertexId = geometry->model().vertices().begin()->first;
    kernel.send(SelectRequested{SelectMode::Replace, EntityRef{EntityKind::Vertex, vertexId}, SelectExpand::None});
    ASSERT_FALSE(selection->empty());  // selection did change...
    EXPECT_FALSE(document->dirty());   // ...but that alone never marks the document dirty
}

TEST_F(DocumentCommandTest, OpenFailureLeavesStoresAndDocumentStoreUntouched) {
    kernel.send(AddEdgeRequested{{0, 0, 0}, {1, 0, 0}});
    const std::size_t vertexCountBefore = geometry->model().vertices().size();
    const std::string pathBefore = document->filePath();
    const bool dirtyBefore = document->dirty();

    kernel.send(OpenDocumentRequested{tempFilePath("does_not_exist.plr")});

    EXPECT_FALSE(ioFailedMessages.empty());
    EXPECT_EQ(geometry->model().vertices().size(), vertexCountBefore);
    EXPECT_EQ(document->filePath(), pathBefore);
    EXPECT_EQ(document->dirty(), dirtyBefore);
}

TEST_F(DocumentCommandTest, SaveFailureDispatchesIoFailedAndLeavesDirty) {
    ASSERT_TRUE(tempDir.isValid());
    kernel.send(AddEdgeRequested{{0, 0, 0}, {1, 0, 0}});
    ASSERT_TRUE(document->dirty());

    // A parent directory that doesn't exist -- QSaveFile::open() fails
    // rather than creating it.
    kernel.send(SaveDocumentRequested{tempFilePath("no_such_subdir/model.plr")});

    EXPECT_FALSE(ioFailedMessages.empty());
    EXPECT_TRUE(document->dirty());
    EXPECT_TRUE(document->filePath().empty());  // setSaved() was never reached
}

TEST_F(DocumentCommandTest, CameraNavigatedUpdatesStoreWithoutFiringCameraChanged) {
    kernel.send(CameraNavigated{{1, 2, 3}, 10.0, 20.0, 30.0, 40.0});

    EXPECT_EQ(camera->state().target.x, 1.0);
    EXPECT_EQ(camera->state().azimuthDeg, 10.0);
    EXPECT_EQ(camera->state().elevationDeg, 20.0);
    EXPECT_EQ(camera->state().distance, 30.0);
    EXPECT_EQ(camera->state().fovYDeg, 40.0);
    EXPECT_EQ(cameraChangedCount, 0);
}

TEST_F(DocumentCommandTest, RestoreCameraFiresCameraChangedExactlyOnce) {
    camera->restoreCamera(CameraState{{9, 8, 7}, 1.0, 2.0, 3.0, 4.0});

    EXPECT_EQ(camera->state().target.x, 9.0);
    EXPECT_EQ(cameraChangedCount, 1);

    camera->restoreCamera(CameraState{{9, 8, 7}, 1.0, 2.0, 3.0, 4.0});  // identical state -- still unconditional
    EXPECT_EQ(cameraChangedCount, 2);
}

}  // namespace
