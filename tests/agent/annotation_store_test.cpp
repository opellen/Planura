#include "agent/annotation_store.h"

#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

#include <ordo/core/kernel.h>

#include "agent/command/annotation_commands.h"
#include "agent/events.h"
#include "agent/command/geometry_commands.h"
#include "agent/geometry_api.h"

#include <geo/model.h>
#include <geo/vec3.h>

#include <gtest/gtest.h>

namespace {

using ordo::core::Kernel;
using plnr::agent::AddDimensionCommand;
using plnr::agent::AddEdgeCommand;
using plnr::agent::AddLeaderTextCommand;
using plnr::agent::AddScreenTextCommand;
using plnr::agent::AnnotationStore;
using plnr::agent::Dimension;
using plnr::agent::GeometryChangedCommand;
using plnr::agent::GeometryApi;
using plnr::agent::kAnnotationStoreName;
using plnr::agent::kGeometryApiName;
using plnr::agent::MoveEntityCommand;
using plnr::agent::RemoveAllAnnotationsCommand;
using plnr::agent::RemoveAnnotationCommand;
using plnr::agent::RemoveEdgeCommand;
using plnr::agent::SetAnnotationTextCommand;
using plnr::agent::TextNote;
using plnr::events::AddDimensionRequested;
using plnr::events::AddEdgeRequested;
using plnr::events::AddLeaderTextRequested;
using plnr::events::AddScreenTextRequested;
using plnr::events::AnnotationsChanged;
using plnr::events::EntityRef;
using plnr::events::MoveEntityRequested;
using plnr::events::RemoveAllAnnotationsRequested;
using plnr::events::RemoveAnnotationRequested;
using plnr::events::SetAnnotationTextRequested;
using plnr::geo::EntityKind;
using plnr::geo::Id;
using plnr::geo::Vec3;

// Wires a kernel with GeometryApi + AnnotationStore, every annotation
// command, and GeometryChangedCommand's GeometryChanged reaction, plus an
// AnnotationsChanged counter for assertions.
class AnnotationChainTest : public ::testing::Test {
protected:
    void SetUp() override {
        kernel.registerAgent(std::make_shared<GeometryApi>());
        kernel.registerAgent(std::make_shared<AnnotationStore>());
        kernel.registerCommand<AddEdgeRequested, AddEdgeCommand>();
        kernel.registerCommand<plnr::events::RemoveEdgeRequested, RemoveEdgeCommand>();
        kernel.registerCommand<MoveEntityRequested, MoveEntityCommand>();
        kernel.registerCommand<plnr::events::GeometryChanged, GeometryChangedCommand>();
        kernel.registerCommand<AddDimensionRequested, AddDimensionCommand>();
        kernel.registerCommand<AddScreenTextRequested, AddScreenTextCommand>();
        kernel.registerCommand<AddLeaderTextRequested, AddLeaderTextCommand>();
        kernel.registerCommand<SetAnnotationTextRequested, SetAnnotationTextCommand>();
        kernel.registerCommand<RemoveAnnotationRequested, RemoveAnnotationCommand>();
        kernel.registerCommand<RemoveAllAnnotationsRequested, RemoveAllAnnotationsCommand>();
        kernel.dispatcher().subscribe<AnnotationsChanged>(&changedCount, [this](const AnnotationsChanged&) { ++changedCount; });

        geometry = kernel.agentAs<GeometryApi>(kGeometryApiName);
        annotations = kernel.agentAs<AnnotationStore>(kAnnotationStoreName);
    }

    // Adds an edge (0,0,0)-(1,0,0), returns its two endpoint vertex ids via
    // Model::findVertex (Model::vertices() is an unordered_map, so
    // iteration order can't tell them apart).
    std::pair<Id, Id> addTestEdge() {
        kernel.send(AddEdgeRequested{Vec3{0, 0, 0}, Vec3{1, 0, 0}});
        const plnr::geo::Vertex* va = geometry->model().findVertex(Vec3{0, 0, 0});
        const plnr::geo::Vertex* vb = geometry->model().findVertex(Vec3{1, 0, 0});
        return {va ? va->id : 0, vb ? vb->id : 0};
    }

    Kernel kernel;
    std::shared_ptr<GeometryApi> geometry;
    std::shared_ptr<AnnotationStore> annotations;
    int changedCount = 0;
};

TEST_F(AnnotationChainTest, StoreStartsEmpty) {
    ASSERT_NE(annotations, nullptr);
    EXPECT_TRUE(annotations->dimensions().empty());
    EXPECT_TRUE(annotations->texts().empty());
}

TEST_F(AnnotationChainTest, AddDimensionRequestedAddsDimensionAndFiresOnce) {
    const auto [vertexA, vertexB] = addTestEdge();
    changedCount = 0;

    kernel.send(AddDimensionRequested{vertexA, vertexB, Vec3{0, 0, 1}, 0.5});

    ASSERT_EQ(annotations->dimensions().size(), 1u);
    const Dimension& dim = annotations->dimensions().front();
    EXPECT_EQ(dim.vertexA, vertexA);
    EXPECT_EQ(dim.vertexB, vertexB);
    EXPECT_EQ(dim.offset, 0.5);
    EXPECT_TRUE(dim.associated);
    EXPECT_EQ(dim.lastA.x, 0.0);
    EXPECT_EQ(dim.lastB.x, 1.0);
    EXPECT_TRUE(dim.overrideText.empty());
    EXPECT_EQ(changedCount, 1);
}

TEST_F(AnnotationChainTest, AddDimensionWithUnknownVerticesStartsNonAssociated) {
    kernel.send(AddDimensionRequested{4242, 4343, Vec3{0, 0, 1}, 1.0});

    ASSERT_EQ(annotations->dimensions().size(), 1u);
    EXPECT_FALSE(annotations->dimensions().front().associated);
}

TEST_F(AnnotationChainTest, AddScreenTextRequestedAddsScreenNoteAndFiresOnce) {
    kernel.send(AddScreenTextRequested{10.0, 20.0, "hello"});

    ASSERT_EQ(annotations->texts().size(), 1u);
    const TextNote& note = annotations->texts().front();
    EXPECT_TRUE(note.screenFixed);
    EXPECT_EQ(note.screenX, 10.0);
    EXPECT_EQ(note.screenY, 20.0);
    EXPECT_EQ(note.text, "hello");
    EXPECT_FALSE(note.leaderTarget.has_value());
    EXPECT_EQ(changedCount, 1);
}

TEST_F(AnnotationChainTest, AddLeaderTextRequestedAddsLeaderNoteAndFiresOnce) {
    kernel.send(AddLeaderTextRequested{Vec3{1, 2, 3}, EntityRef{EntityKind::Face, 7}, "area"});

    ASSERT_EQ(annotations->texts().size(), 1u);
    const TextNote& note = annotations->texts().front();
    EXPECT_FALSE(note.screenFixed);
    EXPECT_EQ(note.worldAnchor.x, 1.0);
    ASSERT_TRUE(note.leaderTarget.has_value());
    EXPECT_EQ(note.leaderTarget->id, 7u);
    EXPECT_EQ(note.text, "area");
    EXPECT_EQ(changedCount, 1);
}

TEST_F(AnnotationChainTest, SetTextOnDimensionSetsOverrideTextAndFiresOnce) {
    const auto [vertexA, vertexB] = addTestEdge();
    kernel.send(AddDimensionRequested{vertexA, vertexB, Vec3{0, 0, 1}, 0.0});
    const Id id = annotations->dimensions().front().id;
    changedCount = 0;

    kernel.send(SetAnnotationTextRequested{id, "custom"});

    EXPECT_EQ(annotations->dimensions().front().overrideText, "custom");
    EXPECT_EQ(changedCount, 1);
}

TEST_F(AnnotationChainTest, SetTextOnDimensionSameValueIsANoOp) {
    const auto [vertexA, vertexB] = addTestEdge();
    kernel.send(AddDimensionRequested{vertexA, vertexB, Vec3{0, 0, 1}, 0.0});
    const Id id = annotations->dimensions().front().id;
    kernel.send(SetAnnotationTextRequested{id, "custom"});
    changedCount = 0;

    kernel.send(SetAnnotationTextRequested{id, "custom"});

    EXPECT_EQ(changedCount, 0);
}

TEST_F(AnnotationChainTest, SetTextOnTextNoteSetsTextAndFiresOnce) {
    kernel.send(AddScreenTextRequested{0.0, 0.0, "old"});
    const Id id = annotations->texts().front().id;
    changedCount = 0;

    kernel.send(SetAnnotationTextRequested{id, "new"});

    EXPECT_EQ(annotations->texts().front().text, "new");
    EXPECT_EQ(changedCount, 1);
}

TEST_F(AnnotationChainTest, SetTextWithUnknownIdIsANoOp) {
    kernel.send(SetAnnotationTextRequested{424242, "x"});

    EXPECT_EQ(changedCount, 0);
}

TEST_F(AnnotationChainTest, RemoveAnnotationRemovesDimensionAndFiresOnce) {
    const auto [vertexA, vertexB] = addTestEdge();
    kernel.send(AddDimensionRequested{vertexA, vertexB, Vec3{0, 0, 1}, 0.0});
    const Id id = annotations->dimensions().front().id;
    changedCount = 0;

    kernel.send(RemoveAnnotationRequested{id});

    EXPECT_TRUE(annotations->dimensions().empty());
    EXPECT_EQ(changedCount, 1);
}

TEST_F(AnnotationChainTest, RemoveAnnotationRemovesTextNoteAndFiresOnce) {
    kernel.send(AddScreenTextRequested{0.0, 0.0, "x"});
    const Id id = annotations->texts().front().id;
    changedCount = 0;

    kernel.send(RemoveAnnotationRequested{id});

    EXPECT_TRUE(annotations->texts().empty());
    EXPECT_EQ(changedCount, 1);
}

TEST_F(AnnotationChainTest, RemoveAnnotationWithUnknownIdIsANoOp) {
    kernel.send(RemoveAnnotationRequested{424242});

    EXPECT_EQ(changedCount, 0);
}

TEST_F(AnnotationChainTest, RemoveAllAnnotationsClearsEverythingAndFiresOnce) {
    const auto [vertexA, vertexB] = addTestEdge();
    kernel.send(AddDimensionRequested{vertexA, vertexB, Vec3{0, 0, 1}, 0.0});
    kernel.send(AddScreenTextRequested{0.0, 0.0, "x"});
    changedCount = 0;

    kernel.send(RemoveAllAnnotationsRequested{});

    EXPECT_TRUE(annotations->dimensions().empty());
    EXPECT_TRUE(annotations->texts().empty());
    EXPECT_EQ(changedCount, 1);
}

TEST_F(AnnotationChainTest, RemoveAllAnnotationsWhenEmptyIsANoOp) {
    kernel.send(RemoveAllAnnotationsRequested{});

    EXPECT_EQ(changedCount, 0);
}

// Removing the edge garbage-collects its endpoint vertices, so the
// dimension anchored to them goes non-associated -- exercised via
// GeometryChangedCommand's reaction, NOT by calling refreshAssociations() directly.
TEST_F(AnnotationChainTest, AssociationBreaksOnVertexRemovalViaGeometryChanged) {
    const auto [vertexA, vertexB] = addTestEdge();
    const Id edgeId = geometry->model().edges().begin()->first;
    kernel.send(AddDimensionRequested{vertexA, vertexB, Vec3{0, 0, 1}, 0.25});
    ASSERT_TRUE(annotations->dimensions().front().associated);
    const Vec3 lastA = annotations->dimensions().front().lastA;
    const Vec3 lastB = annotations->dimensions().front().lastB;
    changedCount = 0;

    kernel.send(plnr::events::RemoveEdgeRequested{edgeId});

    ASSERT_EQ(annotations->dimensions().size(), 1u);  // record kept, not erased
    const Dimension& dim = annotations->dimensions().front();
    EXPECT_FALSE(dim.associated);
    // Last-known positions frozen, not zeroed/changed.
    EXPECT_EQ(dim.lastA.x, lastA.x);
    EXPECT_EQ(dim.lastB.x, lastB.x);
    EXPECT_GE(changedCount, 1);  // AnnotationsChanged fired (association flag changed)
}

// Moving an associated dimension's endpoint vertex updates lastA/lastB and
// fires AnnotationsChanged, without breaking association.
TEST_F(AnnotationChainTest, AssociatedDimensionTracksVertexMove) {
    const auto [vertexA, vertexB] = addTestEdge();
    kernel.send(AddDimensionRequested{vertexA, vertexB, Vec3{0, 0, 1}, 0.0});
    changedCount = 0;

    kernel.send(MoveEntityRequested{EntityKind::Vertex, vertexA, Vec3{5.0, 0.0, 0.0}});

    ASSERT_EQ(annotations->dimensions().size(), 1u);
    const Dimension& dim = annotations->dimensions().front();
    EXPECT_TRUE(dim.associated);
    EXPECT_EQ(dim.lastA.x, 5.0);  // moved from (0,0,0) by delta (5,0,0)
    EXPECT_GE(changedCount, 1);
}

TEST(AnnotationStoreUnregisteredTest, AddScreenTextBeforeRegistrationThrowsBecauseContextIsUnset) {
    AnnotationStore agent;
    EXPECT_THROW(agent.addScreenText(0.0, 0.0, "x"), std::logic_error);
}

TEST(AnnotationStoreUnregisteredTest, RemoveOnEmptyUnregisteredStoreDoesNotThrow) {
    // remove() on an unknown id sends nothing, so it never touches context()
    // -- no agent registration required, mirroring
    // GuideStoreUnregisteredTest::EraseOnEmptyUnregisteredStoreDoesNotThrow.
    AnnotationStore agent;
    EXPECT_NO_THROW(EXPECT_FALSE(agent.remove(1)));
}

TEST(AnnotationStoreUnregisteredTest, RefreshAssociationsOnEmptyUnregisteredStoreDoesNotThrow) {
    // No dimensions to iterate -- never touches context()/send() either.
    AnnotationStore agent;
    plnr::geo::Model model;
    EXPECT_NO_THROW(EXPECT_FALSE(agent.refreshAssociations(model)));
}

}  // namespace
