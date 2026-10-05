#include "agent/selection_store.h"

#include <algorithm>
#include <array>
#include <memory>
#include <stdexcept>
#include <vector>

#include <ordo/core/kernel.h>

#include "agent/command/geometry_commands.h"
#include "agent/command/selection_commands.h"
#include "agent/events.h"
#include "agent/geometry_api.h"

#include <geo/entity.h>
#include <geo/pick.h>
#include <geo/vec3.h>

#include <gtest/gtest.h>

namespace {

using ordo::core::Kernel;
using plnr::agent::AddEdgeCommand;
using plnr::agent::GeometryApi;
using plnr::agent::kGeometryApiName;
using plnr::agent::kSelectionStoreName;
using plnr::agent::PruneSelectionCommand;
using plnr::agent::RemoveEdgeCommand;
using plnr::agent::SelectAllCommand;
using plnr::agent::SelectCommand;
using plnr::agent::SelectionStore;
using plnr::agent::SelectRegionCommand;
using plnr::agent::SetHiddenCommand;
using plnr::agent::UnhideAllCommand;
using plnr::events::AddEdgeRequested;
using plnr::events::EntityRef;
using plnr::events::GeometryChanged;
using plnr::events::RemoveEdgeRequested;
using plnr::events::SelectAllRequested;
using plnr::events::SelectExpand;
using plnr::events::SelectionChanged;
using plnr::events::SelectMode;
using plnr::events::SelectRegionRequested;
using plnr::events::SelectRequested;
using plnr::events::SetHiddenRequested;
using plnr::events::UnhideAllRequested;
using plnr::geo::EntityKind;

// 4 converging corner rays (TL, TR, BR, BL) for a drag rectangle on z=0, eye above center;
// converging, not parallel, avoids degenerate frustum side planes.
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

// GeometryApi + SelectionStore and the select/prune commands; no EditContextStore/TagStore,
// so SelectAllCommand's absent-agent path is exercised.
class SelectionChainTest : public ::testing::Test {
protected:
  void SetUp() override {
    kernel.registerAgent(std::make_shared<GeometryApi>());
    kernel.registerAgent(std::make_shared<SelectionStore>());
    kernel.registerCommand<AddEdgeRequested, AddEdgeCommand>();
    kernel.registerCommand<RemoveEdgeRequested, RemoveEdgeCommand>();
    kernel.registerCommand<plnr::events::GeometryChanged,
                           PruneSelectionCommand>();
    kernel.registerCommand<SelectRequested, SelectCommand>();
    kernel.registerCommand<SelectRegionRequested, SelectRegionCommand>();
    kernel.registerCommand<SelectAllRequested, SelectAllCommand>();
    kernel.registerCommand<SetHiddenRequested, SetHiddenCommand>();
    kernel.registerCommand<UnhideAllRequested, UnhideAllCommand>();
    kernel.dispatcher().subscribe<SelectionChanged>(
        &changedCount, [this](const SelectionChanged &) { ++changedCount; });
    kernel.dispatcher().subscribe<GeometryChanged>(
        &geometryChangedCount,
        [this](const GeometryChanged &) { ++geometryChangedCount; });
    geometry = kernel.agentAs<GeometryApi>(kGeometryApiName);
    selection = kernel.agentAs<SelectionStore>(kSelectionStoreName);
  }

  Kernel kernel;
  std::shared_ptr<GeometryApi> geometry;
  std::shared_ptr<SelectionStore> selection;
  int changedCount = 0;
  int geometryChangedCount = 0;
};

TEST_F(SelectionChainTest, ReplaceSetsItemsAndFiresOnce) {
  ASSERT_NE(selection, nullptr);

  const std::vector<EntityRef> refs{{EntityKind::Vertex, 1},
                                    {EntityKind::Edge, 2}};
  const bool changed = selection->replace(refs);

  EXPECT_TRUE(changed);
  EXPECT_EQ(selection->items(), refs);
  EXPECT_EQ(changedCount, 1);
}

TEST_F(SelectionChainTest, IdenticalReplaceIsANoOpAndDoesNotRefire) {
  const std::vector<EntityRef> refs{{EntityKind::Vertex, 1},
                                    {EntityKind::Edge, 2}};
  selection->replace(refs);
  ASSERT_EQ(changedCount, 1);

  const bool changed = selection->replace(refs); // same ordered set

  EXPECT_FALSE(changed);
  EXPECT_EQ(changedCount, 1); // no second SelectionChanged
}

TEST_F(SelectionChainTest, ReplaceDedupesFirstOccurrenceWinsAndPreservesOrder) {
  const EntityRef v1{EntityKind::Vertex, 1};
  const EntityRef e2{EntityKind::Edge, 2};

  selection->replace({v1, e2, v1}); // duplicate v1 -- first occurrence wins

  const std::vector<EntityRef> expected{v1, e2};
  EXPECT_EQ(selection->items(), expected);
  EXPECT_EQ(changedCount, 1);
}

TEST_F(SelectionChainTest, AddAppendsOnlyNewRefs) {
  const EntityRef v1{EntityKind::Vertex, 1};
  const EntityRef e2{EntityKind::Edge, 2};
  selection->replace({v1});
  ASSERT_EQ(changedCount, 1);

  const bool changed =
      selection->add({v1, e2}); // v1 already selected -- only e2 is new

  EXPECT_TRUE(changed);
  const std::vector<EntityRef> expected{v1, e2};
  EXPECT_EQ(selection->items(), expected);
  EXPECT_EQ(changedCount, 2);
}

TEST_F(SelectionChainTest, AddWithAllRefsAlreadySelectedIsANoOp) {
  const EntityRef v1{EntityKind::Vertex, 1};
  selection->replace({v1});
  ASSERT_EQ(changedCount, 1);

  const bool changed = selection->add({v1}); // duplicate add

  EXPECT_FALSE(changed);
  EXPECT_EQ(changedCount, 1); // no second SelectionChanged
}

TEST_F(SelectionChainTest, ToggleRemovesSelectedAndAddsUnselected) {
  const EntityRef v1{EntityKind::Vertex, 1};
  const EntityRef e2{EntityKind::Edge, 2};
  selection->replace({v1});
  ASSERT_EQ(changedCount, 1);

  const bool changed = selection->toggle(
      {v1, e2}); // v1 selected -> removed, e2 unselected -> added

  EXPECT_TRUE(changed);
  EXPECT_FALSE(selection->contains(v1));
  EXPECT_TRUE(selection->contains(e2));
  EXPECT_EQ(changedCount, 2);
}

TEST_F(SelectionChainTest, SubtractRemovesSelectedRefs) {
  const EntityRef v1{EntityKind::Vertex, 1};
  const EntityRef e2{EntityKind::Edge, 2};
  selection->replace({v1, e2});
  ASSERT_EQ(changedCount, 1);

  const bool changed = selection->subtract({v1});

  EXPECT_TRUE(changed);
  const std::vector<EntityRef> expected{e2};
  EXPECT_EQ(selection->items(), expected);
  EXPECT_EQ(changedCount, 2);
}

TEST_F(SelectionChainTest, SubtractingNonSelectedRefIsANoOp) {
  const EntityRef v1{EntityKind::Vertex, 1};
  const EntityRef e2{EntityKind::Edge, 2};
  selection->replace({v1});
  ASSERT_EQ(changedCount, 1);

  const bool changed = selection->subtract({e2}); // e2 was never selected

  EXPECT_FALSE(changed);
  EXPECT_EQ(changedCount, 1);
}

TEST_F(SelectionChainTest, ClearEmptiesSelectionAndFiresOnce) {
  selection->replace({{EntityKind::Vertex, 1}});
  ASSERT_EQ(changedCount, 1);

  const bool changed = selection->clear();

  EXPECT_TRUE(changed);
  EXPECT_TRUE(selection->empty());
  EXPECT_EQ(changedCount, 2);
}

TEST_F(SelectionChainTest, ClearWhenAlreadyEmptyIsANoOp) {
  ASSERT_TRUE(selection->empty());

  const bool changed = selection->clear();

  EXPECT_FALSE(changed);
  EXPECT_EQ(changedCount, 0);
}

TEST_F(SelectionChainTest, PruneKeepsOrderAndOnlyFiresOnChange) {
  const EntityRef v1{EntityKind::Vertex, 1};
  const EntityRef e2{EntityKind::Edge, 2};
  const EntityRef f3{EntityKind::Face, 3};
  selection->replace({v1, e2, f3});
  ASSERT_EQ(changedCount, 1);

  // Drop e2 only -- order of survivors (v1, f3) must be preserved.
  const bool changed =
      selection->prune([&e2](const EntityRef &ref) { return !(ref == e2); });

  EXPECT_TRUE(changed);
  const std::vector<EntityRef> expected{v1, f3};
  EXPECT_EQ(selection->items(), expected);
  EXPECT_EQ(changedCount, 2);
}

TEST_F(SelectionChainTest, PruneThatKeepsEverythingIsANoOp) {
  const EntityRef v1{EntityKind::Vertex, 1};
  selection->replace({v1});
  ASSERT_EQ(changedCount, 1);

  const bool changed = selection->prune([](const EntityRef &) { return true; });

  EXPECT_FALSE(changed);
  EXPECT_EQ(changedCount, 1);
}

TEST_F(SelectionChainTest,
       RemoveEdgeRequestedPrunesSelectedEdgeAndFiresSelectionChanged) {
  kernel.send(AddEdgeRequested{{0, 0, 0}, {1, 0, 0}});
  ASSERT_EQ(geometry->model().edges().size(), 1u);
  const plnr::geo::Id edgeId = geometry->model().edges().begin()->first;

  selection->replace({{EntityKind::Edge, edgeId}});
  ASSERT_EQ(changedCount, 1);

  kernel.send(
      RemoveEdgeRequested{edgeId}); // GeometryChanged -> PruneSelectionCommand -> selection drops it

  EXPECT_TRUE(selection->empty());
  EXPECT_EQ(changedCount, 2);
}

TEST_F(SelectionChainTest,
       SelectRequestedReplaceWithEdgeTargetSelectsItAndFiresOnce) {
  kernel.send(AddEdgeRequested{{0, 0, 0}, {1, 0, 0}});
  ASSERT_EQ(geometry->model().edges().size(), 1u);
  const plnr::geo::Id edgeId = geometry->model().edges().begin()->first;
  const EntityRef edgeRef{EntityKind::Edge, edgeId};

  kernel.send(
      SelectRequested{SelectMode::Replace, edgeRef, SelectExpand::None});

  EXPECT_TRUE(selection->contains(edgeRef));
  EXPECT_EQ(selection->items().size(), 1u);
  EXPECT_EQ(changedCount, 1);
}

TEST_F(SelectionChainTest, SelectRequestedReplaceWithNoTargetClearsSelection) {
  selection->replace({{EntityKind::Vertex, 1}});
  ASSERT_EQ(changedCount, 1);

  kernel.send(
      SelectRequested{SelectMode::Replace, std::nullopt, SelectExpand::None});

  EXPECT_TRUE(selection->empty());
  EXPECT_EQ(changedCount, 2);
}

TEST_F(SelectionChainTest,
       SelectRequestedReplaceWithNoTargetOnEmptySelectionIsANoOp) {
  ASSERT_TRUE(selection->empty());

  kernel.send(
      SelectRequested{SelectMode::Replace, std::nullopt, SelectExpand::None});

  EXPECT_EQ(changedCount, 0);
}

TEST_F(SelectionChainTest, SelectRequestedAddAppendsSecondEntity) {
  const EntityRef v1{EntityKind::Vertex, 1};
  const EntityRef e2{EntityKind::Edge, 2};
  selection->replace({v1});
  ASSERT_EQ(changedCount, 1);

  kernel.send(SelectRequested{SelectMode::Add, e2, SelectExpand::None});

  const std::vector<EntityRef> expected{v1, e2};
  EXPECT_EQ(selection->items(), expected);
  EXPECT_EQ(changedCount, 2);
}

TEST_F(SelectionChainTest, SelectRequestedToggleOnSelectedEntityRemovesIt) {
  const EntityRef v1{EntityKind::Vertex, 1};
  selection->replace({v1});
  ASSERT_EQ(changedCount, 1);

  kernel.send(SelectRequested{SelectMode::Toggle, v1, SelectExpand::None});

  EXPECT_FALSE(selection->contains(v1));
  EXPECT_EQ(changedCount, 2);
}

TEST_F(SelectionChainTest, SelectRequestedSubtractRemovesEntity) {
  const EntityRef v1{EntityKind::Vertex, 1};
  const EntityRef e2{EntityKind::Edge, 2};
  selection->replace({v1, e2});
  ASSERT_EQ(changedCount, 1);

  kernel.send(SelectRequested{SelectMode::Subtract, v1, SelectExpand::None});

  const std::vector<EntityRef> expected{e2};
  EXPECT_EQ(selection->items(), expected);
  EXPECT_EQ(changedCount, 2);
}

TEST_F(SelectionChainTest, SelectRequestedAddWithNoTargetIsANoOp) {
  ASSERT_TRUE(selection->empty());

  kernel.send(
      SelectRequested{SelectMode::Add, std::nullopt, SelectExpand::None});

  EXPECT_TRUE(selection->empty());
  EXPECT_EQ(changedCount, 0);
}

TEST_F(SelectionChainTest,
       SelectRequestedReplaceAttachedOnFaceSelectsFaceAndFourEdges) {
  // Rectangle with a closing edge -> exactly one face.
  kernel.send(AddEdgeRequested{{0, 0, 0}, {4, 0, 0}});
  kernel.send(AddEdgeRequested{{4, 0, 0}, {4, 3, 0}});
  kernel.send(AddEdgeRequested{{4, 3, 0}, {0, 3, 0}});
  kernel.send(AddEdgeRequested{{0, 3, 0}, {0, 0, 0}});
  ASSERT_EQ(geometry->model().faces().size(), 1u);
  const plnr::geo::Id faceId = geometry->model().faces().begin()->first;
  const EntityRef faceRef{EntityKind::Face, faceId};
  changedCount = 0;

  kernel.send(
      SelectRequested{SelectMode::Replace, faceRef, SelectExpand::Attached});

  EXPECT_EQ(selection->items().size(), 5u); // 1 face + 4 boundary edges
  EXPECT_TRUE(selection->contains(faceRef));
  for (const auto &[id, edge] : geometry->model().edges()) {
    (void)edge;
    EXPECT_TRUE(selection->contains({EntityKind::Edge, id}));
  }
  EXPECT_EQ(changedCount, 1);
}

TEST_F(SelectionChainTest,
       SelectRequestedReplaceAttachedOnEdgeSelectsEdgeAndTwoVertices) {
  kernel.send(AddEdgeRequested{{0, 0, 0}, {1, 0, 0}});
  ASSERT_EQ(geometry->model().edges().size(), 1u);
  const plnr::geo::Id edgeId = geometry->model().edges().begin()->first;
  const EntityRef edgeRef{EntityKind::Edge, edgeId};
  changedCount = 0;

  kernel.send(
      SelectRequested{SelectMode::Replace, edgeRef, SelectExpand::Attached});

  EXPECT_EQ(selection->items().size(), 3u); // edge + 2 endpoint vertices
  EXPECT_TRUE(selection->contains(edgeRef));
  for (const auto &[id, vertex] : geometry->model().vertices()) {
    (void)vertex;
    EXPECT_TRUE(selection->contains({EntityKind::Vertex, id}));
  }
  EXPECT_EQ(changedCount, 1);
}

TEST_F(
    SelectionChainTest,
    SelectRequestedReplaceConnectedFromEdgeOfFacedRectangleSelectsWholeLoop) {
  kernel.send(AddEdgeRequested{{0, 0, 0}, {4, 0, 0}});
  kernel.send(AddEdgeRequested{{4, 0, 0}, {4, 3, 0}});
  kernel.send(AddEdgeRequested{{4, 3, 0}, {0, 3, 0}});
  kernel.send(AddEdgeRequested{{0, 3, 0}, {0, 0, 0}});
  ASSERT_EQ(geometry->model().faces().size(), 1u);
  ASSERT_EQ(geometry->model().edges().size(), 4u);
  ASSERT_EQ(geometry->model().vertices().size(), 4u);
  const plnr::geo::Id edgeId = geometry->model().edges().begin()->first;
  const EntityRef edgeRef{EntityKind::Edge, edgeId};
  changedCount = 0;

  kernel.send(
      SelectRequested{SelectMode::Replace, edgeRef, SelectExpand::Connected});

  // 4 vertices + 4 edges + 1 face = 9 total.
  EXPECT_EQ(selection->items().size(), 9u);
  for (const auto &[id, vertex] : geometry->model().vertices()) {
    (void)vertex;
    EXPECT_TRUE(selection->contains({EntityKind::Vertex, id}));
  }
  for (const auto &[id, edge] : geometry->model().edges()) {
    (void)edge;
    EXPECT_TRUE(selection->contains({EntityKind::Edge, id}));
  }
  for (const auto &[id, face] : geometry->model().faces()) {
    (void)face;
    EXPECT_TRUE(selection->contains({EntityKind::Face, id}));
  }
  EXPECT_EQ(changedCount, 1);
}

TEST_F(SelectionChainTest,
       SelectRequestedAddConnectedMergesIntoExistingSelection) {
  kernel.send(AddEdgeRequested{{0, 0, 0}, {4, 0, 0}});
  kernel.send(AddEdgeRequested{{4, 0, 0}, {4, 3, 0}});
  kernel.send(AddEdgeRequested{{4, 3, 0}, {0, 3, 0}});
  kernel.send(AddEdgeRequested{{0, 3, 0}, {0, 0, 0}});
  ASSERT_EQ(geometry->model().faces().size(), 1u);
  const plnr::geo::Id faceId = geometry->model().faces().begin()->first;
  const plnr::geo::Id edgeId = geometry->model().edges().begin()->first;

  // Pre-existing selection unrelated to the rectangle.
  const EntityRef preexisting{EntityKind::Vertex, 999};
  selection->replace({preexisting});
  changedCount = 0;

  kernel.send(SelectRequested{SelectMode::Add,
                              EntityRef{EntityKind::Edge, edgeId},
                              SelectExpand::Connected});

  EXPECT_TRUE(selection->contains(preexisting));
  EXPECT_TRUE(selection->contains({EntityKind::Face, faceId}));
  EXPECT_EQ(selection->items().size(),
            10u); // preexisting + 4 vertices + 4 edges + 1 face
  EXPECT_EQ(changedCount, 1);
}

TEST_F(SelectionChainTest,
       SelectRequestedExpandWithStaleTargetIdDoesNotCrashAndSelectsTargetOnly) {
  // Stale id: connectedComponent returns empty, so only the target ref is selected
  // (liveness is PruneSelectionCommand's job).
  const EntityRef staleEdge{EntityKind::Edge, 424242};

  kernel.send(
      SelectRequested{SelectMode::Replace, staleEdge, SelectExpand::Connected});

  EXPECT_EQ(selection->items().size(), 1u);
  EXPECT_TRUE(selection->contains(staleEdge));
  EXPECT_EQ(changedCount, 1);
}

TEST_F(
    SelectionChainTest,
    SelectRegionRequestedWindowReplaceOnFullyContainingRectSelectsWholeRectangle) {
  kernel.send(AddEdgeRequested{{0, 0, 0}, {4, 0, 0}});
  kernel.send(AddEdgeRequested{{4, 0, 0}, {4, 3, 0}});
  kernel.send(AddEdgeRequested{{4, 3, 0}, {0, 3, 0}});
  kernel.send(AddEdgeRequested{{0, 3, 0}, {0, 0, 0}});
  ASSERT_EQ(geometry->model().faces().size(), 1u);
  changedCount = 0;

  kernel.send(SelectRegionRequested{
      SelectMode::Replace, cornerRaysAbove(-1.0, 5.0, -1.0, 4.0), false});

  EXPECT_EQ(selection->items().size(), 9u); // 4 vertices + 4 edges + 1 face
  for (const auto &[id, vertex] : geometry->model().vertices()) {
    (void)vertex;
    EXPECT_TRUE(selection->contains({EntityKind::Vertex, id}));
  }
  for (const auto &[id, edge] : geometry->model().edges()) {
    (void)edge;
    EXPECT_TRUE(selection->contains({EntityKind::Edge, id}));
  }
  for (const auto &[id, face] : geometry->model().faces()) {
    (void)face;
    EXPECT_TRUE(selection->contains({EntityKind::Face, id}));
  }
  EXPECT_EQ(changedCount, 1);
}

TEST_F(SelectionChainTest,
       SelectRegionRequestedWindowHalfCoveringRectSelectsOnlyContainedSubset) {
  kernel.send(AddEdgeRequested{{0, 0, 0}, {4, 0, 0}});
  kernel.send(AddEdgeRequested{{4, 0, 0}, {4, 3, 0}});
  kernel.send(AddEdgeRequested{{4, 3, 0}, {0, 3, 0}});
  kernel.send(AddEdgeRequested{{0, 3, 0}, {0, 0, 0}});
  ASSERT_EQ(geometry->model().faces().size(), 1u);
  changedCount = 0;

  // Left half (x in [-1, 2]): both x=0 vertices are in, so the x=0 edge is contained; the face (all 4) is not.
  kernel.send(SelectRegionRequested{
      SelectMode::Replace, cornerRaysAbove(-1.0, 2.0, -1.0, 4.0), false});

  EXPECT_EQ(selection->items().size(), 3u); // 2 vertices + 1 edge
  for (const auto &[id, face] : geometry->model().faces()) {
    (void)face;
    EXPECT_FALSE(selection->contains({EntityKind::Face, id}));
  }
  EXPECT_EQ(changedCount, 1);
}

TEST_F(SelectionChainTest, SelectRegionRequestedCrossingOverOneEdgeSelectsIt) {
  kernel.send(AddEdgeRequested{{0, 0, 0}, {4, 0, 0}});
  ASSERT_EQ(geometry->model().edges().size(), 1u);
  const plnr::geo::Id edgeId = geometry->model().edges().begin()->first;
  changedCount = 0;

  // Thin box over the edge midsection (x in [1.5, 2.5]): no endpoint inside, only the crossing rule picks it.
  kernel.send(SelectRegionRequested{
      SelectMode::Replace, cornerRaysAbove(1.5, 2.5, -1.0, 1.0), true});

  EXPECT_TRUE(selection->contains({EntityKind::Edge, edgeId}));
  EXPECT_EQ(selection->items().size(), 1u);
  EXPECT_EQ(changedCount, 1);
}

TEST_F(SelectionChainTest,
       SelectRegionRequestedReplaceWithEmptyRegionClearsSelection) {
  kernel.send(AddEdgeRequested{{0, 0, 0}, {1, 0, 0}});
  selection->replace(
      {{EntityKind::Vertex, 999}}); // unrelated pre-existing selection
  changedCount = 0;

  // Region far from any geometry: empty result, like a no-target Replace.
  kernel.send(SelectRegionRequested{
      SelectMode::Replace, cornerRaysAbove(100.0, 101.0, 100.0, 101.0), false});

  EXPECT_TRUE(selection->empty());
  EXPECT_EQ(changedCount, 1);
}

TEST_F(
    SelectionChainTest,
    SetHiddenRequestedHidesFaceFiresGeometryChangedOnceAndAutoSubtractsSelection) {
  kernel.send(AddEdgeRequested{{0, 0, 0}, {4, 0, 0}});
  kernel.send(AddEdgeRequested{{4, 0, 0}, {4, 3, 0}});
  kernel.send(AddEdgeRequested{{4, 3, 0}, {0, 3, 0}});
  kernel.send(AddEdgeRequested{{0, 3, 0}, {0, 0, 0}});
  ASSERT_EQ(geometry->model().faces().size(), 1u);
  const plnr::geo::Id faceId = geometry->model().faces().begin()->first;
  const EntityRef faceRef{EntityKind::Face, faceId};

  selection->replace({faceRef});
  changedCount = 0;
  geometryChangedCount = 0;

  kernel.send(SetHiddenRequested{{faceRef}, true});

  EXPECT_TRUE(geometry->isHidden(faceRef));
  EXPECT_EQ(geometryChangedCount, 1);
  // the reference modeler: hiding an entity deselects it too (SetHiddenCommand's job).
  EXPECT_FALSE(selection->contains(faceRef));
  EXPECT_TRUE(selection->empty());
  EXPECT_EQ(changedCount, 1);
}

TEST_F(SelectionChainTest, SetHiddenRequestedOnAlreadyHiddenRefIsANoOp) {
  kernel.send(AddEdgeRequested{{0, 0, 0}, {1, 0, 0}});
  ASSERT_EQ(geometry->model().edges().size(), 1u);
  const plnr::geo::Id edgeId = geometry->model().edges().begin()->first;
  const EntityRef edgeRef{EntityKind::Edge, edgeId};

  kernel.send(SetHiddenRequested{{edgeRef}, true});
  ASSERT_TRUE(geometry->isHidden(edgeRef));
  geometryChangedCount = 0;
  changedCount = 0;

  kernel.send(SetHiddenRequested{{edgeRef}, true}); // already hidden -- no-op

  EXPECT_TRUE(geometry->isHidden(edgeRef));
  EXPECT_EQ(geometryChangedCount, 0); // no second GeometryChanged
  EXPECT_EQ(changedCount,
            0); // nothing was selected, so no SelectionChanged either
}

TEST_F(SelectionChainTest,
       UnhideAllRequestedRestoresFiringOnceAndNoOpWhenNothingHidden) {
  kernel.send(AddEdgeRequested{{0, 0, 0}, {1, 0, 0}});
  ASSERT_EQ(geometry->model().edges().size(), 1u);
  const plnr::geo::Id edgeId = geometry->model().edges().begin()->first;
  const EntityRef edgeRef{EntityKind::Edge, edgeId};

  kernel.send(SetHiddenRequested{{edgeRef}, true});
  ASSERT_TRUE(geometry->isHidden(edgeRef));
  geometryChangedCount = 0;

  kernel.send(UnhideAllRequested{});

  EXPECT_FALSE(geometry->isHidden(edgeRef));
  EXPECT_TRUE(geometry->hidden().empty());
  EXPECT_EQ(geometryChangedCount, 1);

  kernel.send(UnhideAllRequested{}); // nothing hidden now -- no-op

  EXPECT_EQ(geometryChangedCount, 1); // no second GeometryChanged
}

TEST_F(SelectionChainTest, RemoveEdgeRequestedPrunesDeadRefsFromHiddenSet) {
  kernel.send(AddEdgeRequested{{0, 0, 0}, {1, 0, 0}});
  ASSERT_EQ(geometry->model().edges().size(), 1u);
  const plnr::geo::Id edgeId = geometry->model().edges().begin()->first;
  const EntityRef edgeRef{EntityKind::Edge, edgeId};

  kernel.send(SetHiddenRequested{{edgeRef}, true});
  ASSERT_TRUE(geometry->isHidden(edgeRef));

  kernel.send(RemoveEdgeRequested{
      edgeId}); // deletes the edge -- pruneHiddenDeadRefs should drop its ref

  EXPECT_EQ(geometry->hidden().count(edgeRef), 0u);
  EXPECT_TRUE(geometry->hidden().empty());
}

TEST_F(SelectionChainTest, SelectRegionRequestedSkipsAHiddenEntity) {
  kernel.send(AddEdgeRequested{{0, 0, 0}, {4, 0, 0}});
  kernel.send(AddEdgeRequested{{4, 0, 0}, {4, 3, 0}});
  kernel.send(AddEdgeRequested{{4, 3, 0}, {0, 3, 0}});
  kernel.send(AddEdgeRequested{{0, 3, 0}, {0, 0, 0}});
  ASSERT_EQ(geometry->model().faces().size(), 1u);
  const plnr::geo::Id firstEdgeId = geometry->model().edges().begin()->first;
  const EntityRef hiddenEdgeRef{EntityKind::Edge, firstEdgeId};

  kernel.send(SetHiddenRequested{{hiddenEdgeRef}, true});
  ASSERT_TRUE(geometry->isHidden(hiddenEdgeRef));
  changedCount = 0;

  // Window-select the whole rectangle: the hidden edge must not qualify as an Edge pick; the face is unaffected.
  kernel.send(SelectRegionRequested{
      SelectMode::Replace, cornerRaysAbove(-1.0, 5.0, -1.0, 4.0), false});

  EXPECT_FALSE(selection->contains(hiddenEdgeRef));
  for (const auto &[id, edge] : geometry->model().edges()) {
    (void)edge;
    if (id == firstEdgeId)
      continue;
    EXPECT_TRUE(selection->contains(
        {EntityKind::Edge, id})); // the other 3 edges are unaffected
  }
}

// -- SelectAllRequested ---------------------------
// No TagStore is registered, so SelectAllCommand's tag filter isn't exercised.

TEST_F(SelectionChainTest,
       SelectAllRequestedSelectsAllFacesEdgesVerticesSortedFacesFirst) {
  kernel.send(AddEdgeRequested{{0, 0, 0}, {4, 0, 0}});
  kernel.send(AddEdgeRequested{{4, 0, 0}, {4, 3, 0}});
  kernel.send(AddEdgeRequested{{4, 3, 0}, {0, 3, 0}});
  kernel.send(AddEdgeRequested{{0, 3, 0}, {0, 0, 0}});
  ASSERT_EQ(geometry->model().faces().size(), 1u);
  ASSERT_EQ(geometry->model().edges().size(), 4u);
  ASSERT_EQ(geometry->model().vertices().size(), 4u);
  changedCount = 0;

  kernel.send(SelectAllRequested{});

  // Expected order: ids per kind, sorted ascending (asserts the sort, not insertion order).
  std::vector<plnr::geo::Id> faceIds;
  for (const auto &[id, face] : geometry->model().faces()) {
    (void)face;
    faceIds.push_back(id);
  }
  std::sort(faceIds.begin(), faceIds.end());
  std::vector<plnr::geo::Id> edgeIds;
  for (const auto &[id, edge] : geometry->model().edges()) {
    (void)edge;
    edgeIds.push_back(id);
  }
  std::sort(edgeIds.begin(), edgeIds.end());
  std::vector<plnr::geo::Id> vertexIds;
  for (const auto &[id, vertex] : geometry->model().vertices()) {
    (void)vertex;
    vertexIds.push_back(id);
  }
  std::sort(vertexIds.begin(), vertexIds.end());

  std::vector<EntityRef> expected;
  for (plnr::geo::Id id : faceIds)
    expected.push_back({EntityKind::Face, id});
  for (plnr::geo::Id id : edgeIds)
    expected.push_back({EntityKind::Edge, id});
  for (plnr::geo::Id id : vertexIds)
    expected.push_back({EntityKind::Vertex, id});

  EXPECT_EQ(selection->items(), expected);
  EXPECT_EQ(changedCount, 1);
}

TEST_F(SelectionChainTest, SelectAllRequestedExcludesHiddenEntities) {
  kernel.send(AddEdgeRequested{{0, 0, 0}, {4, 0, 0}});
  kernel.send(AddEdgeRequested{{4, 0, 0}, {4, 3, 0}});
  kernel.send(AddEdgeRequested{{4, 3, 0}, {0, 3, 0}});
  kernel.send(AddEdgeRequested{{0, 3, 0}, {0, 0, 0}});
  ASSERT_EQ(geometry->model().faces().size(), 1u);
  const plnr::geo::Id firstEdgeId = geometry->model().edges().begin()->first;
  const EntityRef hiddenEdgeRef{EntityKind::Edge, firstEdgeId};
  kernel.send(SetHiddenRequested{{hiddenEdgeRef}, true});
  ASSERT_TRUE(geometry->isHidden(hiddenEdgeRef));
  changedCount = 0;

  kernel.send(SelectAllRequested{});

  EXPECT_FALSE(selection->contains(hiddenEdgeRef));
  for (const auto &[id, edge] : geometry->model().edges()) {
    (void)edge;
    if (id == firstEdgeId)
      continue;
    EXPECT_TRUE(selection->contains(
        {EntityKind::Edge, id})); // the other 3 edges are unaffected
  }
  // The hidden edge's endpoints are shared by visible edges, so they stay selected; only the edge ref is excluded.
  EXPECT_EQ(changedCount, 1);
}

TEST_F(SelectionChainTest, SelectAllRequestedReplacesPreExistingSelection) {
  kernel.send(AddEdgeRequested{{0, 0, 0}, {1, 0, 0}});
  ASSERT_EQ(geometry->model().edges().size(), 1u);
  const plnr::geo::Id edgeId = geometry->model().edges().begin()->first;

  // Select All wholly replaces a pre-existing selection (no modifier mode).
  selection->replace({{EntityKind::Vertex, 999}});
  changedCount = 0;

  kernel.send(SelectAllRequested{});

  EXPECT_FALSE(selection->contains({EntityKind::Vertex, 999}));
  EXPECT_TRUE(selection->contains({EntityKind::Edge, edgeId}));
  EXPECT_EQ(changedCount, 1);
}

TEST_F(SelectionChainTest, SelectAllRequestedOnEmptyModelClearsSelection) {
  selection->replace({{EntityKind::Vertex, 999}});
  changedCount = 0;

  kernel.send(SelectAllRequested{}); // nothing in the model -- replace() with empty refs

  EXPECT_TRUE(selection->empty());
  EXPECT_EQ(changedCount, 1);
}

TEST(SelectionStoreUnregisteredTest,
     ReplaceBeforeRegistrationThrowsBecauseContextIsUnset) {
  SelectionStore agent;
  EXPECT_THROW(agent.replace({{EntityKind::Vertex, 1}}), std::logic_error);
}

TEST(SelectionStoreUnregisteredTest,
     ClearOnEmptyUnregisteredStoreDoesNotThrow) {
  // clear() on an already-empty agent sends nothing, so no context() registration is needed.
  SelectionStore agent;
  EXPECT_NO_THROW(EXPECT_FALSE(agent.clear()));
}

} // namespace
