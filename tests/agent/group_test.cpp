#include "agent/command/group_commands.h"

#include <array>
#include <memory>
#include <vector>

#include <ordo/core/kernel.h>

#include <geo/entity.h>
#include <geo/scene.h>
#include <geo/vec3.h>

#include "agent/command/geometry_commands.h"
#include "agent/command/selection_commands.h"
#include "agent/events.h"
#include "agent/geometry_api.h"
#include "agent/selection_store.h"

#include <gtest/gtest.h>

namespace {

using ordo::core::Kernel;
using plnr::agent::AddEdgeCommand;
using plnr::agent::ExplodeCommand;
using plnr::agent::GeometryApi;
using plnr::agent::GroupCreateCommand;
using plnr::agent::kGeometryApiName;
using plnr::agent::kSelectionStoreName;
using plnr::agent::PruneSelectionCommand;
using plnr::agent::SelectCommand;
using plnr::agent::SelectionStore;
using plnr::agent::SelectRegionCommand;
using plnr::agent::SetHiddenCommand;
using plnr::events::AddEdgeRequested;
using plnr::events::EntityRef;
using plnr::events::ExplodeRequested;
using plnr::events::GeometryChanged;
using plnr::events::GroupCreateRequested;
using plnr::events::SelectExpand;
using plnr::events::SelectionChanged;
using plnr::events::SelectMode;
using plnr::events::SelectRegionRequested;
using plnr::events::SelectRequested;
using plnr::events::SetHiddenRequested;
using plnr::geo::Definition;
using plnr::geo::EntityKind;
using plnr::geo::Id;
using plnr::geo::Instance;
using plnr::geo::kInvalidId;

// 4 converging corner rays for a drag rectangle on z=0, eye above center looking straight down.
std::array<plnr::geo::Ray, 4> cornerRaysAbove(double minX, double maxX,
                                              double minY, double maxY) {
  using plnr::geo::normalized;
  using plnr::geo::Vec3;
  const Vec3 eye{(minX + maxX) / 2.0, (minY + maxY) / 2.0, 10.0};
  const Vec3 tl{minX, maxY, 0.0};
  const Vec3 tr{maxX, maxY, 0.0};
  const Vec3 br{maxX, minY, 0.0};
  const Vec3 bl{minX, minY, 0.0};
  return {
      plnr::geo::Ray{eye, normalized(tl - eye)},
      plnr::geo::Ray{eye, normalized(tr - eye)},
      plnr::geo::Ray{eye, normalized(br - eye)},
      plnr::geo::Ray{eye, normalized(bl - eye)},
  };
}

// Fresh kernel with GeometryApi + SelectionStore, the geometry/group/selection commands, and
// GeometryChanged/SelectionChanged counters; Qt-free, no Presenter.
class GroupChainTest : public ::testing::Test {
protected:
  void SetUp() override {
    kernel.registerAgent(std::make_shared<GeometryApi>());
    kernel.registerAgent(std::make_shared<SelectionStore>());
    kernel.registerCommand<AddEdgeRequested, AddEdgeCommand>();
    kernel.registerCommand<SetHiddenRequested, SetHiddenCommand>();
    kernel.registerCommand<GroupCreateRequested, GroupCreateCommand>();
    kernel.registerCommand<ExplodeRequested, ExplodeCommand>();
    kernel.registerCommand<GeometryChanged, PruneSelectionCommand>();
    kernel.registerCommand<SelectRequested, SelectCommand>();
    kernel.registerCommand<SelectRegionRequested, SelectRegionCommand>();
    kernel.dispatcher().subscribe<GeometryChanged>(
        &geometryChangedCount,
        [this](const GeometryChanged &) { ++geometryChangedCount; });
    kernel.dispatcher().subscribe<SelectionChanged>(
        &selectionChangedCount,
        [this](const SelectionChanged &) { ++selectionChangedCount; });
    geometry = kernel.agentAs<GeometryApi>(kGeometryApiName);
    selection = kernel.agentAs<SelectionStore>(kSelectionStoreName);
  }

  // Builds a 4-edge rectangle closing into one face; returns its face id.
  Id buildRectangle() {
    kernel.send(AddEdgeRequested{{0, 0, 0}, {4, 0, 0}});
    kernel.send(AddEdgeRequested{{4, 0, 0}, {4, 3, 0}});
    kernel.send(AddEdgeRequested{{4, 3, 0}, {0, 3, 0}});
    kernel.send(AddEdgeRequested{{0, 3, 0}, {0, 0, 0}});
    return geometry->model().faces().begin()->first;
  }

  // Selects the face + its 4 edges + 4 vertices, as a triple-click Select would.
  void selectWholeRectangle(Id faceId) {
    std::vector<EntityRef> refs{{EntityKind::Face, faceId}};
    for (const auto &[id, edge] : geometry->model().edges()) {
      (void)edge;
      refs.push_back({EntityKind::Edge, id});
    }
    for (const auto &[id, vertex] : geometry->model().vertices()) {
      (void)vertex;
      refs.push_back({EntityKind::Vertex, id});
    }
    selection->replace(refs);
  }

  Kernel kernel;
  std::shared_ptr<GeometryApi> geometry;
  std::shared_ptr<SelectionStore> selection;
  int geometryChangedCount = 0;
  int selectionChangedCount = 0;
};

TEST_F(GroupChainTest,
       GroupCreateRequestedMovesSelectionIntoANewInstanceAndClearsSelection) {
  const Id faceId = buildRectangle();
  selectWholeRectangle(faceId);
  ASSERT_EQ(geometry->model().edges().size(), 4u);
  ASSERT_EQ(geometry->model().faces().size(), 1u);
  const int changedBefore = geometryChangedCount;
  const int selChangedBefore = selectionChangedCount;

  kernel.send(GroupCreateRequested{false, ""});

  EXPECT_TRUE(geometry->model().edges().empty());
  EXPECT_TRUE(geometry->model().faces().empty());
  ASSERT_EQ(geometry->scene().root().children.size(), 1u);
  const Instance &inst = geometry->scene().root().children[0];
  const Definition *def = geometry->scene().definition(inst.definitionId);
  ASSERT_NE(def, nullptr);
  EXPECT_EQ(def->model.edges().size(), 4u);
  EXPECT_EQ(def->model.faces().size(), 1u);
  EXPECT_TRUE(def->isGroup); // asComponent == false
  EXPECT_EQ(geometryChangedCount, changedBefore + 1);
  EXPECT_TRUE(selection->empty());
  EXPECT_EQ(selectionChangedCount, selChangedBefore + 1);
}

TEST_F(GroupChainTest, GroupCreateRequestedWithEmptySelectionIsANoOp) {
  buildRectangle();
  ASSERT_TRUE(selection->empty());
  const int changedBefore = geometryChangedCount;

  kernel.send(GroupCreateRequested{false, ""});

  EXPECT_EQ(geometry->scene().root().children.size(), 0u);
  EXPECT_EQ(geometryChangedCount, changedBefore);
}

TEST_F(GroupChainTest, GroupCreateRequestedAsComponentSetsIsGroupFalse) {
  const Id faceId = buildRectangle();
  selectWholeRectangle(faceId);

  kernel.send(GroupCreateRequested{true, ""});

  ASSERT_EQ(geometry->scene().root().children.size(), 1u);
  const Instance &inst = geometry->scene().root().children[0];
  const Definition *def = geometry->scene().definition(inst.definitionId);
  ASSERT_NE(def, nullptr);
  EXPECT_FALSE(def->isGroup);
}

// Instance transforms are identity here: GeometryApi::scene() is const-only.
// Translation is covered in tests/geo/scene_ops_test.cpp.
TEST_F(GroupChainTest, ExplodeRequestedRestoresFourEdgesAndOneFaceAtIdentity) {
  const Id faceId = buildRectangle();
  selectWholeRectangle(faceId);
  kernel.send(GroupCreateRequested{false, ""});
  ASSERT_EQ(geometry->scene().root().children.size(), 1u);
  const Id instanceId = geometry->scene().root().children[0].id;
  ASSERT_TRUE(geometry->model().edges().empty());
  const int changedBefore = geometryChangedCount;

  kernel.send(ExplodeRequested{instanceId});

  EXPECT_EQ(geometry->model().edges().size(), 4u);
  EXPECT_EQ(geometry->model().faces().size(), 1u);
  EXPECT_EQ(geometry->scene().root().children.size(), 0u);
  EXPECT_EQ(geometryChangedCount, changedBefore + 1);
}

TEST_F(GroupChainTest, ExplodeRequestedWithUnknownIdIsANoOp) {
  buildRectangle();
  const int changedBefore = geometryChangedCount;

  kernel.send(ExplodeRequested{999999});

  EXPECT_EQ(geometryChangedCount, changedBefore);
}

TEST_F(GroupChainTest, HiddenRefOnAGroupedEdgeIsPruned) {
  const Id faceId = buildRectangle();
  const Id edgeId = geometry->model().edges().begin()->first;
  kernel.send(SetHiddenRequested{{{EntityKind::Edge, edgeId}}, true});
  ASSERT_TRUE(geometry->isHidden({EntityKind::Edge, edgeId}));

  selectWholeRectangle(faceId);
  kernel.send(GroupCreateRequested{false, ""});

  // The old edge id is gone: its hidden_ entry must be pruned too (as removeEdge/extrudeFace do).
  EXPECT_TRUE(geometry->hidden().empty());
}

// -- Instance selection chain tests -------------------------------

TEST_F(GroupChainTest, SelectRequestedTargetingTheInstanceSelectsIt) {
  const Id faceId = buildRectangle();
  selectWholeRectangle(faceId);
  kernel.send(GroupCreateRequested{false, ""});
  ASSERT_EQ(geometry->scene().root().children.size(), 1u);
  const Id instanceId = geometry->scene().root().children[0].id;
  const EntityRef instRef{EntityKind::Instance, instanceId};

  kernel.send(
      SelectRequested{SelectMode::Replace, instRef, SelectExpand::None});

  EXPECT_TRUE(selection->contains(instRef));
  EXPECT_EQ(selection->items().size(), 1u);
}

TEST_F(GroupChainTest, PruneSelectionKeepsAliveInstanceAndDropsItAfterExplode) {
  const Id faceId = buildRectangle();
  selectWholeRectangle(faceId);
  kernel.send(GroupCreateRequested{false, ""});
  ASSERT_EQ(geometry->scene().root().children.size(), 1u);
  const Id instanceId = geometry->scene().root().children[0].id;
  const EntityRef instRef{EntityKind::Instance, instanceId};

  selection->replace({instRef});
  ASSERT_TRUE(selection->contains(instRef));

  // An unrelated GeometryChanged triggers PruneSelectionCommand; the instance is still a root child, so it survives.
  kernel.send(AddEdgeRequested{{50, 50, 0}, {51, 50, 0}});
  EXPECT_TRUE(selection->contains(instRef));

  // Explode removes the instance; the GeometryChanged it fires prunes the dead ref.
  kernel.send(ExplodeRequested{instanceId});

  EXPECT_FALSE(selection->contains(instRef));
  EXPECT_TRUE(selection->empty());
}

TEST_F(
    GroupChainTest,
    ExplodeRequestedWithZeroIdExplodesSelectedInstancesAndDropsThemFromSelection) {
  const Id faceId = buildRectangle();
  selectWholeRectangle(faceId);
  kernel.send(GroupCreateRequested{false, ""});
  ASSERT_EQ(geometry->scene().root().children.size(), 1u);
  const Id instanceId = geometry->scene().root().children[0].id;
  const EntityRef instRef{EntityKind::Instance, instanceId};

  selection->replace({instRef});
  ASSERT_TRUE(geometry->model().edges().empty());

  kernel.send(
      ExplodeRequested{kInvalidId}); // 0 -- explode every selected Instance

  EXPECT_EQ(geometry->model().edges().size(), 4u);
  EXPECT_EQ(geometry->model().faces().size(), 1u);
  EXPECT_EQ(geometry->scene().root().children.size(), 0u);
  EXPECT_TRUE(selection->empty());
}

TEST_F(GroupChainTest, ExplodeRequestedWithZeroIdAndNoInstanceSelectedIsANoOp) {
  buildRectangle();
  const int changedBefore = geometryChangedCount;

  kernel.send(ExplodeRequested{kInvalidId});

  EXPECT_EQ(geometryChangedCount, changedBefore);
}

TEST_F(GroupChainTest, HiddenInstanceIsSkippedByRegionSelect) {
  const Id faceId = buildRectangle();
  selectWholeRectangle(faceId);
  kernel.send(GroupCreateRequested{false, ""});
  ASSERT_EQ(geometry->scene().root().children.size(), 1u);
  const Id instanceId = geometry->scene().root().children[0].id;
  const EntityRef instRef{EntityKind::Instance, instanceId};

  kernel.send(SetHiddenRequested{{instRef}, true});
  ASSERT_TRUE(geometry->isHidden(instRef));

  // Window-select over the instance footprint (0<=x<=4, 0<=y<=3); the hidden instance must not qualify.
  kernel.send(SelectRegionRequested{
      SelectMode::Replace, cornerRaysAbove(-1.0, 5.0, -1.0, 4.0), false});

  EXPECT_FALSE(selection->contains(instRef));
}

TEST(GroupUnregisteredTest, GroupCreateAndExplodeWithNoStoresDoNotCrash) {
  Kernel kernel; // GeometryApi/SelectionStore deliberately not registered
  kernel.registerCommand<GroupCreateRequested, GroupCreateCommand>();
  kernel.registerCommand<ExplodeRequested, ExplodeCommand>();

  EXPECT_NO_THROW(kernel.send(GroupCreateRequested{false, ""}));
  EXPECT_NO_THROW(kernel.send(ExplodeRequested{1}));
}

} // namespace
