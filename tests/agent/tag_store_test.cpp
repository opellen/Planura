#include "agent/tag_store.h"

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include <ordo/core/kernel.h>

#include "agent/events.h"
#include "agent/command/geometry_commands.h"
#include "agent/geometry_api.h"
#include "agent/command/selection_commands.h"
#include "agent/selection_store.h"
#include "agent/command/tag_commands.h"

#include <geo/entity.h>
#include <geo/vec3.h>

#include <gtest/gtest.h>

namespace {

using ordo::core::Kernel;
using plnr::agent::AddEdgeCommand;
using plnr::agent::GeometryApi;
using plnr::agent::kGeometryApiName;
using plnr::agent::kSelectionStoreName;
using plnr::agent::kTagStoreName;
using plnr::agent::kUntaggedTagId;
using plnr::agent::PruneSelectionCommand;
using plnr::agent::SelectCommand;
using plnr::agent::SelectionStore;
using plnr::agent::SelectRegionCommand;
using plnr::agent::TagAssignCommand;
using plnr::agent::TagCreateCommand;
using plnr::agent::TagStore;
using plnr::agent::TagVisibilityCommand;
using plnr::events::AddEdgeRequested;
using plnr::events::EntityRef;
using plnr::events::SelectMode;
using plnr::events::SelectRegionRequested;
using plnr::events::SelectRequested;
using plnr::events::TagAssignRequested;
using plnr::events::TagCreateRequested;
using plnr::events::TagsChanged;
using plnr::events::TagVisibilityRequested;
using plnr::geo::EntityKind;

// Builds 4 converging corner rays for a screen-axis-aligned drag rectangle
// [minX,maxX] x [minY,maxY] on z = 0.
std::array<plnr::geo::Ray, 4> cornerRaysAbove(double minX, double maxX, double minY, double maxY) {
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

// Wires a kernel with GeometryApi + SelectionStore + TagStore, the
// geometry/selection commands needed for the region-select-skips-invisible-
// tag scenario, and all three tag commands. Qt-free by design.
class TagChainTest : public ::testing::Test {
protected:
    void SetUp() override {
        kernel.registerAgent(std::make_shared<GeometryApi>());
        kernel.registerAgent(std::make_shared<SelectionStore>());
        kernel.registerAgent(std::make_shared<TagStore>());
        kernel.registerCommand<AddEdgeRequested, AddEdgeCommand>();
        kernel.registerCommand<plnr::events::GeometryChanged, PruneSelectionCommand>();
        kernel.registerCommand<SelectRequested, SelectCommand>();
        kernel.registerCommand<SelectRegionRequested, SelectRegionCommand>();
        kernel.registerCommand<TagCreateRequested, TagCreateCommand>();
        kernel.registerCommand<TagAssignRequested, TagAssignCommand>();
        kernel.registerCommand<TagVisibilityRequested, TagVisibilityCommand>();
        kernel.dispatcher().subscribe<plnr::events::SelectionChanged>(
            &selectionChangedCount, [this](const plnr::events::SelectionChanged&) { ++selectionChangedCount; });
        kernel.dispatcher().subscribe<TagsChanged>(&tagsChangedCount, [this](const TagsChanged&) { ++tagsChangedCount; });
        geometry = kernel.agentAs<GeometryApi>(kGeometryApiName);
        selection = kernel.agentAs<SelectionStore>(kSelectionStoreName);
        tags = kernel.agentAs<TagStore>(kTagStoreName);
    }

    // Creates a tag named "Walls" via the real event path and returns its
    // id, resetting tagsChangedCount to 0 afterward so callers can assert on
    // just their own subsequent action.
    std::uint64_t createWallsTag() {
        kernel.send(TagCreateRequested{"Walls"});
        tagsChangedCount = 0;
        return tags->tags().back().id;
    }

    Kernel kernel;
    std::shared_ptr<GeometryApi> geometry;
    std::shared_ptr<SelectionStore> selection;
    std::shared_ptr<TagStore> tags;
    int selectionChangedCount = 0;
    int tagsChangedCount = 0;
};

TEST_F(TagChainTest, DefaultUntaggedTagExistsAndIsVisible) {
    ASSERT_EQ(tags->tags().size(), 1u);
    EXPECT_EQ(tags->tags().front().id, kUntaggedTagId);
    EXPECT_EQ(tags->tags().front().name, "Untagged");
    EXPECT_TRUE(tags->tags().front().visible);
}

TEST_F(TagChainTest, TagCreateRequestedWithEmptyNameAutoNamesAndFiresOnce) {
    kernel.send(TagCreateRequested{""});

    ASSERT_EQ(tags->tags().size(), 2u);
    EXPECT_EQ(tags->tags().back().name, "Tag 2");  // N = the new tag's id
    EXPECT_EQ(tagsChangedCount, 1);
}

TEST_F(TagChainTest, TagCreateRequestedWithExplicitNameKeepsIt) {
    kernel.send(TagCreateRequested{"Walls"});

    ASSERT_EQ(tags->tags().size(), 2u);
    EXPECT_EQ(tags->tags().back().name, "Walls");
    EXPECT_EQ(tagsChangedCount, 1);
}

TEST_F(TagChainTest, AssignToNewTagMovesEntityAndFiresOnce) {
    const std::uint64_t wallsId = createWallsTag();
    const EntityRef edge{EntityKind::Edge, 1};

    kernel.send(TagAssignRequested{{edge}, wallsId});

    EXPECT_EQ(tags->tagOf(edge), wallsId);
    EXPECT_EQ(tagsChangedCount, 1);
}

TEST_F(TagChainTest, ReassignToSameTagIsANoOp) {
    const std::uint64_t wallsId = createWallsTag();
    const EntityRef edge{EntityKind::Edge, 1};
    kernel.send(TagAssignRequested{{edge}, wallsId});
    tagsChangedCount = 0;

    kernel.send(TagAssignRequested{{edge}, wallsId});  // already on Walls -- no-op

    EXPECT_EQ(tagsChangedCount, 0);
}

TEST_F(TagChainTest, AssignToUntaggedErasesTheMapping) {
    const std::uint64_t wallsId = createWallsTag();
    const EntityRef edge{EntityKind::Edge, 1};
    kernel.send(TagAssignRequested{{edge}, wallsId});
    ASSERT_EQ(tags->tagOf(edge), wallsId);
    tagsChangedCount = 0;

    kernel.send(TagAssignRequested{{edge}, kUntaggedTagId});

    EXPECT_EQ(tags->tagOf(edge), kUntaggedTagId);
    EXPECT_EQ(tagsChangedCount, 1);
}

TEST_F(TagChainTest, AssignWithUnknownTagIdIsANoOp) {
    const EntityRef edge{EntityKind::Edge, 1};

    kernel.send(TagAssignRequested{{edge}, 424242});

    EXPECT_EQ(tags->tagOf(edge), kUntaggedTagId);
    EXPECT_EQ(tagsChangedCount, 0);
}

TEST_F(TagChainTest, VisibilityRequestedFalseHidesTagAndPrunesItsSelectedEntities) {
    kernel.send(AddEdgeRequested{{0, 0, 0}, {4, 0, 0}});
    kernel.send(AddEdgeRequested{{4, 0, 0}, {4, 3, 0}});
    kernel.send(AddEdgeRequested{{4, 3, 0}, {0, 3, 0}});
    kernel.send(AddEdgeRequested{{0, 3, 0}, {0, 0, 0}});
    ASSERT_EQ(geometry->model().faces().size(), 1u);
    const plnr::geo::Id faceId = geometry->model().faces().begin()->first;
    const EntityRef faceRef{EntityKind::Face, faceId};

    const std::uint64_t wallsId = createWallsTag();
    kernel.send(TagAssignRequested{{faceRef}, wallsId});

    selection->replace({faceRef});
    selectionChangedCount = 0;
    tagsChangedCount = 0;

    kernel.send(TagVisibilityRequested{wallsId, false});

    EXPECT_FALSE(tags->isEntityVisible(faceRef));
    EXPECT_EQ(tagsChangedCount, 1);
    EXPECT_TRUE(selection->empty());  // hiding its tag deselects it too
    EXPECT_EQ(selectionChangedCount, 1);
}

TEST_F(TagChainTest, VisibilityRequestedTrueOnAlreadyVisibleTagIsANoOp) {
    kernel.send(TagVisibilityRequested{kUntaggedTagId, true});  // Untagged already visible

    EXPECT_EQ(tagsChangedCount, 0);
    EXPECT_EQ(selectionChangedCount, 0);
}

TEST_F(TagChainTest, VisibilityRequestedUnknownTagIdIsANoOp) {
    kernel.send(TagVisibilityRequested{424242, false});

    EXPECT_EQ(tagsChangedCount, 0);
    EXPECT_EQ(selectionChangedCount, 0);
}

TEST_F(TagChainTest, SelectRegionRequestedSkipsATagInvisibleEntity) {
    kernel.send(AddEdgeRequested{{0, 0, 0}, {4, 0, 0}});
    kernel.send(AddEdgeRequested{{4, 0, 0}, {4, 3, 0}});
    kernel.send(AddEdgeRequested{{4, 3, 0}, {0, 3, 0}});
    kernel.send(AddEdgeRequested{{0, 3, 0}, {0, 0, 0}});
    ASSERT_EQ(geometry->model().faces().size(), 1u);
    const plnr::geo::Id firstEdgeId = geometry->model().edges().begin()->first;
    const EntityRef invisibleEdgeRef{EntityKind::Edge, firstEdgeId};

    const std::uint64_t wallsId = createWallsTag();
    kernel.send(TagAssignRequested{{invisibleEdgeRef}, wallsId});
    kernel.send(TagVisibilityRequested{wallsId, false});
    selectionChangedCount = 0;

    // Window-select the whole rectangle -- the tag-invisible edge must not
    // qualify as an Edge pick even though it's geometrically inside the
    // frustum (pickInFrustum's per-kind filter, wired by SelectRegionCommand's visibilityFilter).
    kernel.send(SelectRegionRequested{SelectMode::Replace, cornerRaysAbove(-1.0, 5.0, -1.0, 4.0), false});

    EXPECT_FALSE(selection->contains(invisibleEdgeRef));
    for (const auto& [id, edge] : geometry->model().edges()) {
        (void)edge;
        if (id == firstEdgeId) continue;
        EXPECT_TRUE(selection->contains({EntityKind::Edge, id}));  // the other 3 edges are unaffected
    }
}

}  // namespace
