#include "agent/edit_context_store.h"

#include <memory>
#include <stdexcept>
#include <vector>

#include <ordo/core/kernel.h>

#include <geo/entity.h>
#include <geo/vec3.h>

#include "agent/command/edit_context_commands.h"
#include "agent/events.h"
#include "agent/command/geometry_commands.h"
#include "agent/geometry_api.h"
#include "agent/command/group_commands.h"
#include "agent/command/selection_commands.h"
#include "agent/selection_store.h"

#include <gtest/gtest.h>

namespace {

using ordo::core::Kernel;
using plnr::agent::AddEdgeCommand;
using plnr::agent::EditContextStore;
using plnr::agent::EnterContextCommand;
using plnr::agent::ExitContextCommand;
using plnr::agent::GeometryApi;
using plnr::agent::GroupCreateCommand;
using plnr::agent::kEditContextStoreName;
using plnr::agent::kGeometryApiName;
using plnr::agent::kSelectionStoreName;
using plnr::agent::SelectCommand;
using plnr::agent::SelectionStore;
using plnr::events::AddEdgeRequested;
using plnr::events::EditContextChanged;
using plnr::events::EnterContextRequested;
using plnr::events::EntityRef;
using plnr::events::ExitContextRequested;
using plnr::events::GroupCreateRequested;
using plnr::events::SelectExpand;
using plnr::events::SelectionChanged;
using plnr::events::SelectMode;
using plnr::events::SelectRequested;
using plnr::geo::Definition;
using plnr::geo::EntityKind;
using plnr::geo::Id;
using plnr::geo::kInvalidId;

// Wires a kernel with GeometryApi + SelectionStore + EditContextStore
// and the commands this suite exercises, plus EditContextChanged/
// SelectionChanged counters on the kernel's dispatcher (Qt-free, no Presenter).
class EditContextChainTest : public ::testing::Test {
protected:
    void SetUp() override {
        kernel.registerAgent(std::make_shared<GeometryApi>());
        kernel.registerAgent(std::make_shared<SelectionStore>());
        kernel.registerAgent(std::make_shared<EditContextStore>());
        kernel.registerCommand<AddEdgeRequested, AddEdgeCommand>();
        kernel.registerCommand<GroupCreateRequested, GroupCreateCommand>();
        kernel.registerCommand<SelectRequested, SelectCommand>();
        kernel.registerCommand<EnterContextRequested, EnterContextCommand>();
        kernel.registerCommand<ExitContextRequested, ExitContextCommand>();
        kernel.dispatcher().subscribe<EditContextChanged>(
            &editContextChangedCount, [this](const EditContextChanged&) { ++editContextChangedCount; });
        kernel.dispatcher().subscribe<SelectionChanged>(&selectionChangedCount,
                                                         [this](const SelectionChanged&) { ++selectionChangedCount; });
        geometry = kernel.agentAs<GeometryApi>(kGeometryApiName);
        selection = kernel.agentAs<SelectionStore>(kSelectionStoreName);
        editContext = kernel.agentAs<EditContextStore>(kEditContextStoreName);
    }

    // Builds a 4-edge rectangle closing into a single face, selects the
    // whole thing, and groups it -- returns the new root-child Instance's id.
    Id buildGroupInstance() {
        kernel.send(AddEdgeRequested{{0, 0, 0}, {4, 0, 0}});
        kernel.send(AddEdgeRequested{{4, 0, 0}, {4, 3, 0}});
        kernel.send(AddEdgeRequested{{4, 3, 0}, {0, 3, 0}});
        kernel.send(AddEdgeRequested{{0, 3, 0}, {0, 0, 0}});
        const Id faceId = geometry->model().faces().begin()->first;

        std::vector<EntityRef> refs{{EntityKind::Face, faceId}};
        for (const auto& [id, edge] : geometry->model().edges()) {
            (void)edge;
            refs.push_back({EntityKind::Edge, id});
        }
        for (const auto& [id, vertex] : geometry->model().vertices()) {
            (void)vertex;
            refs.push_back({EntityKind::Vertex, id});
        }
        selection->replace(refs);

        kernel.send(GroupCreateRequested{false, ""});
        return geometry->scene().root().children.back().id;
    }

    Kernel kernel;
    std::shared_ptr<GeometryApi> geometry;
    std::shared_ptr<SelectionStore> selection;
    std::shared_ptr<EditContextStore> editContext;
    int editContextChangedCount = 0;
    int selectionChangedCount = 0;
};

TEST_F(EditContextChainTest, EnterContextRequestedOnAChildInstancePushesFiresOnceAndClearsSelection) {
    const Id instanceId = buildGroupInstance();
    ASSERT_TRUE(editContext->atRoot());
    selection->replace({{EntityKind::Instance, instanceId}});
    editContextChangedCount = 0;
    selectionChangedCount = 0;

    kernel.send(EnterContextRequested{instanceId});

    EXPECT_FALSE(editContext->atRoot());
    const std::vector<Id> expectedPath{instanceId};
    EXPECT_EQ(editContext->path(), expectedPath);
    EXPECT_EQ(editContextChangedCount, 1);
    // the reference modeler: entering a context deselects.
    EXPECT_TRUE(selection->empty());
    EXPECT_EQ(selectionChangedCount, 1);
}

TEST_F(EditContextChainTest, EnterContextRequestedOnAnUnknownIdIsANoOp) {
    buildGroupInstance();
    editContextChangedCount = 0;
    selectionChangedCount = 0;  // buildGroupInstance's own select+group already fired some

    kernel.send(EnterContextRequested{999999});

    EXPECT_TRUE(editContext->atRoot());
    EXPECT_EQ(editContextChangedCount, 0);
    EXPECT_EQ(selectionChangedCount, 0);
}

TEST_F(EditContextChainTest, EnterContextRequestedRejectsAnIdThatIsNotAChildOfTheCurrentContext) {
    const Id instance1 = buildGroupInstance();
    const Id instance2 = buildGroupInstance();  // a second, SIBLING instance at root
    ASSERT_NE(instance1, instance2);

    kernel.send(EnterContextRequested{instance1});
    ASSERT_EQ(editContext->path(), std::vector<Id>{instance1});
    editContextChangedCount = 0;

    // instance2 exists, but it is NOT a child of instance1's definition
    // (it's a root sibling) -- entering it from inside instance1's context
    // must be rejected.
    kernel.send(EnterContextRequested{instance2});

    EXPECT_EQ(editContext->path(), std::vector<Id>{instance1});  // unchanged
    EXPECT_EQ(editContextChangedCount, 0);
}

TEST_F(EditContextChainTest, ExitContextRequestedAtRootIsANoOpAndDoesNotFireEvent) {
    ASSERT_TRUE(editContext->atRoot());

    kernel.send(ExitContextRequested{});

    EXPECT_TRUE(editContext->atRoot());
    EXPECT_EQ(editContextChangedCount, 0);
}

TEST_F(EditContextChainTest, ExitContextRequestedPopsOneLevelFiresOnceAndClearsSelection) {
    const Id instanceId = buildGroupInstance();
    kernel.send(EnterContextRequested{instanceId});
    ASSERT_FALSE(editContext->atRoot());
    selection->replace({{EntityKind::Vertex, 1}});
    editContextChangedCount = 0;
    selectionChangedCount = 0;

    kernel.send(ExitContextRequested{});

    EXPECT_TRUE(editContext->atRoot());
    EXPECT_EQ(editContextChangedCount, 1);
    EXPECT_TRUE(selection->empty());
    EXPECT_EQ(selectionChangedCount, 1);
}

TEST_F(EditContextChainTest, GeometryAgentContextDefinitionResolvesRootForAnEmptyPath) {
    const Definition* def = geometry->contextDefinition({});
    ASSERT_NE(def, nullptr);
    EXPECT_EQ(def->id, plnr::geo::kRootDefinitionId);
    EXPECT_EQ(geometry->contextDefinition(), def);  // convenience overload matches
}

TEST_F(EditContextChainTest, GeometryAgentContextDefinitionResolvesTheGroupsDefinitionForAValidPath) {
    const Id instanceId = buildGroupInstance();
    const Definition* def = geometry->contextDefinition({instanceId});
    ASSERT_NE(def, nullptr);
    EXPECT_EQ(def->model.edges().size(), 4u);
    EXPECT_EQ(def->model.faces().size(), 1u);
    EXPECT_TRUE(def->isGroup);

    const plnr::geo::Model* model = geometry->contextModel({instanceId});
    ASSERT_NE(model, nullptr);
    EXPECT_EQ(model, &def->model);
}

TEST_F(EditContextChainTest, GeometryAgentContextDefinitionReturnsNullptrForAnUnknownId) {
    buildGroupInstance();

    EXPECT_EQ(geometry->contextDefinition({999999}), nullptr);
    EXPECT_EQ(geometry->contextModel({999999}), nullptr);
}

TEST_F(EditContextChainTest, EnterThenExitReturnsToRootWithAnEmptyPath) {
    const Id instanceId = buildGroupInstance();

    kernel.send(EnterContextRequested{instanceId});
    ASSERT_FALSE(editContext->atRoot());

    kernel.send(ExitContextRequested{});

    EXPECT_TRUE(editContext->atRoot());
    EXPECT_TRUE(editContext->path().empty());
}

TEST(EditContextStoreDirectTest, PushAlwaysFiresAndGrowsThePath) {
    Kernel kernel;
    auto agent = std::make_shared<EditContextStore>();
    kernel.registerAgent(agent);
    int changedCount = 0;
    kernel.dispatcher().subscribe<EditContextChanged>(&changedCount,
                                                       [&changedCount](const EditContextChanged&) { ++changedCount; });

    EXPECT_TRUE(agent->push(42));
    EXPECT_FALSE(agent->atRoot());
    const std::vector<Id> expected{42};
    EXPECT_EQ(agent->path(), expected);
    EXPECT_EQ(changedCount, 1);
}

TEST(EditContextStoreDirectTest, PopAtRootIsANoOp) {
    Kernel kernel;
    auto agent = std::make_shared<EditContextStore>();
    kernel.registerAgent(agent);
    int changedCount = 0;
    kernel.dispatcher().subscribe<EditContextChanged>(&changedCount,
                                                       [&changedCount](const EditContextChanged&) { ++changedCount; });

    EXPECT_FALSE(agent->pop());
    EXPECT_EQ(changedCount, 0);
}

TEST(EditContextStoreDirectTest, ResetClearsAMultiLevelPathInOneEventAndNoOpsAtRoot) {
    Kernel kernel;
    auto agent = std::make_shared<EditContextStore>();
    kernel.registerAgent(agent);
    int changedCount = 0;
    kernel.dispatcher().subscribe<EditContextChanged>(&changedCount,
                                                       [&changedCount](const EditContextChanged&) { ++changedCount; });

    agent->push(1);
    agent->push(2);
    changedCount = 0;

    EXPECT_TRUE(agent->reset());
    EXPECT_TRUE(agent->atRoot());
    EXPECT_EQ(changedCount, 1);

    EXPECT_FALSE(agent->reset());  // already at root now
    EXPECT_EQ(changedCount, 1);
}

TEST(EditContextStoreUnregisteredTest, PushBeforeRegistrationThrowsBecauseContextIsUnset) {
    EditContextStore agent;
    EXPECT_THROW(agent.push(1), std::logic_error);
}

TEST(EditContextStoreUnregisteredTest, PopOnAnUnregisteredStoreAtRootDoesNotThrow) {
    // pop() on an already-root agent sends nothing, so it never touches
    // context() -- no agent registration required (mirrors
    // SelectionStoreUnregisteredTest::ClearOnEmptyUnregisteredStoreDoesNotThrow).
    EditContextStore agent;
    EXPECT_NO_THROW(EXPECT_FALSE(agent.pop()));
}

}  // namespace
