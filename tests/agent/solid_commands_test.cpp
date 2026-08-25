#include "agent/command/solid_commands.h"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include <ordo/core/app_kernel.h>

#include <geo/entity.h>
#include <geo/scene.h>
#include <geo/solid.h>
#include <geo/vec3.h>

#include "agent/document_store.h"
#include "agent/events.h"
#include "agent/geometry_api.h"
#include "agent/material_repository.h"
#include "agent/selection_store.h"
#include "agent/transaction_manager.h"
#include "agent/command/undo_commands.h"
#include "agent/undo_store.h"

#include <gtest/gtest.h>

namespace {

using ordo::core::AppKernel;
using plnr::agent::GeometryApi;
using plnr::agent::kGeometryApiName;
using plnr::agent::kMaterialRepositoryName;
using plnr::agent::kSelectionStoreName;
using plnr::agent::kUndoStoreName;
using plnr::agent::MarkDirtyCommand;
using plnr::agent::MaterialRepository;
using plnr::agent::RedoCommand;
using plnr::agent::SelectionStore;
using plnr::agent::SolidOpCommand;
using plnr::agent::UndoCaptureCommand;
using plnr::agent::UndoCommand;
using plnr::agent::UndoStore;
using plnr::events::EntityRef;
using plnr::events::GeometryChanged;
using plnr::events::MaterialsChanged;
using plnr::events::RedoRequested;
using plnr::events::SolidOp;
using plnr::events::SolidOpRequested;
using plnr::events::StatusHintChanged;
using plnr::events::UndoRequested;
using plnr::geo::Definition;
using plnr::geo::EntityKind;
using plnr::geo::Id;
using plnr::geo::Instance;
using plnr::geo::isSolidDefinition;
using plnr::geo::kRootDefinitionId;
using plnr::geo::solidVolume;
using plnr::geo::Transform;
using plnr::geo::Vec3;

constexpr double kVolTol = 1e-6;

// Builds a closed (or, with omitFace in [0,5], deliberately open)
// axis-aligned box's vertex/face lists for GeometryApi::importMesh --
// every loop wound so its Newell normal points outward.
void boxMesh(Vec3 minP, Vec3 maxP, std::vector<Vec3>& vertices, std::vector<std::vector<std::size_t>>& faces,
             int omitFace = -1) {
    vertices = {
        {minP.x, minP.y, minP.z}, {maxP.x, minP.y, minP.z}, {maxP.x, maxP.y, minP.z}, {minP.x, maxP.y, minP.z},
        {minP.x, minP.y, maxP.z}, {maxP.x, minP.y, maxP.z}, {maxP.x, maxP.y, maxP.z}, {minP.x, maxP.y, maxP.z},
    };
    const std::vector<std::vector<std::size_t>> allFaces = {
        {0, 3, 2, 1},  // bottom, -z
        {4, 5, 6, 7},  // top, +z
        {0, 1, 5, 4},  // front, -y
        {3, 7, 6, 2},  // back, +y
        {0, 4, 7, 3},  // left, -x
        {1, 2, 6, 5},  // right, +x
    };
    faces.clear();
    for (int i = 0; i < 6; ++i) {
        if (i == omitFace) continue;
        faces.push_back(allFaces[static_cast<std::size_t>(i)]);
    }
}

class SolidOpsTest : public ::testing::Test {
protected:
    void SetUp() override {
        kernel.registerAgent(std::make_shared<GeometryApi>());
        kernel.registerAgent(std::make_shared<SelectionStore>());
        kernel.registerAgent(std::make_shared<MaterialRepository>());
        kernel.registerAgent(std::make_shared<UndoStore>());

        // The Intent under test, wrapped in UndoCaptureCommand exactly like
        // main.cpp's own registration.
        kernel.registerCommand<SolidOpRequested, UndoCaptureCommand<SolidOpCommand, SolidOpRequested>>();
        // MaterialsChanged needs a registered reaction so paint()
        // mid-transaction pings UndoStore::notifyMutation() -- otherwise the
        // carry-over test's paint would silently fall outside the undo
        // snapshot (see document_store.h's MarkDirtyCommand comment).
        kernel.registerCommand<MaterialsChanged, MarkDirtyCommand<MaterialsChanged>>();
        // Undo/Redo themselves -- never wrapped (see UndoCommand/RedoCommand's
        // own comments).
        kernel.registerCommand<UndoRequested, UndoCommand>();
        kernel.registerCommand<RedoRequested, RedoCommand>();

        kernel.dispatcher().subscribe<GeometryChanged>(&geometryChangedCount,
                                                        [this](const GeometryChanged&) { ++geometryChangedCount; });
        kernel.dispatcher().subscribe<StatusHintChanged>(
            &lastHint, [this](const StatusHintChanged& e) { lastHint = e.hint; });

        geometry = kernel.agentAs<GeometryApi>(kGeometryApiName);
        selection = kernel.agentAs<SelectionStore>(kSelectionStoreName);
        materials = kernel.agentAs<MaterialRepository>(kMaterialRepositoryName);
        undo = kernel.agentAs<UndoStore>(kUndoStoreName);
        ASSERT_NE(geometry, nullptr);
        ASSERT_NE(selection, nullptr);
        ASSERT_NE(materials, nullptr);
        ASSERT_NE(undo, nullptr);
    }

    // Places one closed (or, with omitFace set, deliberately open) box as a
    // new root Instance via GeometryApi::importMesh -- explicit
    // restoreFace winding, no auto-face-detection risk.
    Id makeBoxInstance(Vec3 minP, Vec3 maxP, int omitFace = -1) {
        std::vector<Vec3> vertices;
        std::vector<std::vector<std::size_t>> faces;
        boxMesh(minP, maxP, vertices, faces, omitFace);
        return geometry->importMesh(std::string(), vertices, faces);
    }

    const Definition* defOf(Id instanceId) const {
        const Instance* inst = geometry->scene().findInstance(kRootDefinitionId, instanceId);
        if (inst == nullptr) return nullptr;
        return geometry->scene().definition(inst->definitionId);
    }

    AppKernel kernel;
    std::shared_ptr<GeometryApi> geometry;
    std::shared_ptr<SelectionStore> selection;
    std::shared_ptr<MaterialRepository> materials;
    std::shared_ptr<UndoStore> undo;
    int geometryChangedCount = 0;
    std::string lastHint;
};

// -- 1. Union -----------------------------------------------------------

TEST_F(SolidOpsTest, UnionOfTwoOverlappingBoxesProducesOneWorldBakedSolidInstance) {
    const Id a = makeBoxInstance({0, 0, 0}, {2, 2, 2});
    const Id b = makeBoxInstance({1, 1, 1}, {3, 3, 3});
    ASSERT_TRUE(isSolidDefinition(*defOf(a)));
    ASSERT_TRUE(isSolidDefinition(*defOf(b)));
    ASSERT_EQ(geometry->scene().root().children.size(), 2u);

    kernel.send(SolidOpRequested{SolidOp::Union, {a, b}});

    ASSERT_EQ(geometry->scene().root().children.size(), 1u);
    const Instance& inst = geometry->scene().root().children[0];
    EXPECT_EQ(inst.name, "Union");
    EXPECT_TRUE(inst.transform.almostEqual(Transform::identity()));
    const Definition* def = geometry->scene().definition(inst.definitionId);
    ASSERT_NE(def, nullptr);
    EXPECT_TRUE(isSolidDefinition(*def));
    EXPECT_NEAR(solidVolume(def->model), 15.0, kVolTol);
    EXPECT_EQ(geometry->scene().findInstance(kRootDefinitionId, a), nullptr);
    EXPECT_EQ(geometry->scene().findInstance(kRootDefinitionId, b), nullptr);
}

// -- 2. Subtract ----------------------------------------------------------

TEST_F(SolidOpsTest, SubtractRemovesBothOperandsProducingDifference) {
    const Id cutter = makeBoxInstance({1, 1, 1}, {3, 3, 3});
    const Id target = makeBoxInstance({0, 0, 0}, {2, 2, 2});

    kernel.send(SolidOpRequested{SolidOp::Subtract, {cutter, target}});

    ASSERT_EQ(geometry->scene().root().children.size(), 1u);
    const Instance& inst = geometry->scene().root().children[0];
    EXPECT_EQ(inst.name, "Difference");
    const Definition* def = geometry->scene().definition(inst.definitionId);
    ASSERT_NE(def, nullptr);
    EXPECT_TRUE(isSolidDefinition(*def));
    EXPECT_NEAR(solidVolume(def->model), 7.0, kVolTol);
    EXPECT_EQ(geometry->scene().findInstance(kRootDefinitionId, cutter), nullptr);
    EXPECT_EQ(geometry->scene().findInstance(kRootDefinitionId, target), nullptr);
}

// -- 3. Trim ----------------------------------------------------------------

TEST_F(SolidOpsTest, TrimKeepsCutterRemovesTarget) {
    const Id cutter = makeBoxInstance({1, 1, 1}, {3, 3, 3});
    const Id target = makeBoxInstance({0, 0, 0}, {2, 2, 2});

    kernel.send(SolidOpRequested{SolidOp::Trim, {cutter, target}});

    ASSERT_EQ(geometry->scene().root().children.size(), 2u);  // Difference + surviving cutter
    EXPECT_NE(geometry->scene().findInstance(kRootDefinitionId, cutter), nullptr);
    EXPECT_EQ(geometry->scene().findInstance(kRootDefinitionId, target), nullptr);

    const Definition* cutterDef = defOf(cutter);
    ASSERT_NE(cutterDef, nullptr);
    EXPECT_NEAR(solidVolume(cutterDef->model), 8.0, kVolTol);  // untouched

    bool foundDifference = false;
    for (const Instance& inst : geometry->scene().root().children) {
        if (inst.name == "Difference") {
            foundDifference = true;
            const Definition* def = geometry->scene().definition(inst.definitionId);
            ASSERT_NE(def, nullptr);
            EXPECT_NEAR(solidVolume(def->model), 7.0, kVolTol);
        }
    }
    EXPECT_TRUE(foundDifference);
}

// -- 4. Intersect + Split -----------------------------------------------

TEST_F(SolidOpsTest, IntersectProducesOverlapVolume) {
    const Id a = makeBoxInstance({0, 0, 0}, {2, 2, 2});
    const Id b = makeBoxInstance({1, 1, 1}, {3, 3, 3});

    kernel.send(SolidOpRequested{SolidOp::Intersect, {a, b}});

    ASSERT_EQ(geometry->scene().root().children.size(), 1u);
    const Instance& inst = geometry->scene().root().children[0];
    EXPECT_EQ(inst.name, "Intersection");
    const Definition* def = geometry->scene().definition(inst.definitionId);
    ASSERT_NE(def, nullptr);
    EXPECT_NEAR(solidVolume(def->model), 1.0, kVolTol);
}

TEST_F(SolidOpsTest, SplitProducesThreeInstancesWithExpectedVolumes) {
    const Id a = makeBoxInstance({0, 0, 0}, {2, 2, 2});
    const Id b = makeBoxInstance({1, 1, 1}, {3, 3, 3});

    kernel.send(SolidOpRequested{SolidOp::Split, {a, b}});

    ASSERT_EQ(geometry->scene().root().children.size(), 3u);
    EXPECT_EQ(geometry->scene().findInstance(kRootDefinitionId, a), nullptr);
    EXPECT_EQ(geometry->scene().findInstance(kRootDefinitionId, b), nullptr);

    std::vector<double> volumes;
    for (const Instance& inst : geometry->scene().root().children) {
        EXPECT_EQ(inst.name, "");
        const Definition* def = geometry->scene().definition(inst.definitionId);
        ASSERT_NE(def, nullptr);
        EXPECT_TRUE(isSolidDefinition(*def));
        volumes.push_back(solidVolume(def->model));
    }
    std::sort(volumes.begin(), volumes.end());
    ASSERT_EQ(volumes.size(), 3u);
    EXPECT_NEAR(volumes[0], 1.0, kVolTol);
    EXPECT_NEAR(volumes[1], 7.0, kVolTol);
    EXPECT_NEAR(volumes[2], 7.0, kVolTol);
}

// -- 5. Arity / validation ------------------------------------------------

TEST_F(SolidOpsTest, UnionWithOnlyOneOperandMutatesNothing) {
    const Id a = makeBoxInstance({0, 0, 0}, {2, 2, 2});
    const int changedBefore = geometryChangedCount;

    kernel.send(SolidOpRequested{SolidOp::Union, {a}});

    EXPECT_EQ(geometryChangedCount, changedBefore);
    ASSERT_EQ(geometry->scene().root().children.size(), 1u);
    EXPECT_NE(geometry->scene().findInstance(kRootDefinitionId, a), nullptr);
    EXPECT_FALSE(lastHint.empty());
}

TEST_F(SolidOpsTest, UnionWithANonSolidOperandMutatesNothing) {
    const Id a = makeBoxInstance({0, 0, 0}, {2, 2, 2});
    const Id open = makeBoxInstance({5, 5, 5}, {6, 6, 6}, /*omitFace=*/1);  // missing top -- not a solid
    ASSERT_FALSE(isSolidDefinition(*defOf(open)));
    const int changedBefore = geometryChangedCount;

    kernel.send(SolidOpRequested{SolidOp::Union, {a, open}});

    EXPECT_EQ(geometryChangedCount, changedBefore);
    ASSERT_EQ(geometry->scene().root().children.size(), 2u);
    EXPECT_NE(geometry->scene().findInstance(kRootDefinitionId, a), nullptr);
    EXPECT_NE(geometry->scene().findInstance(kRootDefinitionId, open), nullptr);
    EXPECT_EQ(lastHint, "Not a solid.");
}

// -- 6. Overlap gate --------------------------------------------------------

TEST_F(SolidOpsTest, DisjointSubtractIsRejectedWithNoMutation) {
    const Id cutter = makeBoxInstance({0, 0, 0}, {1, 1, 1});
    const Id target = makeBoxInstance({5, 5, 5}, {7, 7, 7});
    const int changedBefore = geometryChangedCount;

    kernel.send(SolidOpRequested{SolidOp::Subtract, {cutter, target}});

    EXPECT_EQ(geometryChangedCount, changedBefore);
    ASSERT_EQ(geometry->scene().root().children.size(), 2u);
    EXPECT_NE(geometry->scene().findInstance(kRootDefinitionId, cutter), nullptr);
    EXPECT_NE(geometry->scene().findInstance(kRootDefinitionId, target), nullptr);
    EXPECT_EQ(lastHint, "The solids must overlap.");
}

TEST_F(SolidOpsTest, DisjointUnionProducesOneStillSolidInstanceWithSummedVolume) {
    const Id a = makeBoxInstance({0, 0, 0}, {1, 1, 1});
    const Id b = makeBoxInstance({5, 5, 5}, {7, 7, 7});

    kernel.send(SolidOpRequested{SolidOp::Union, {a, b}});

    ASSERT_EQ(geometry->scene().root().children.size(), 1u);
    const Instance& inst = geometry->scene().root().children[0];
    const Definition* def = geometry->scene().definition(inst.definitionId);
    ASSERT_NE(def, nullptr);
    EXPECT_TRUE(isSolidDefinition(*def));  // two disjoint shells, still a valid manifold solid
    EXPECT_NEAR(solidVolume(def->model), 1.0 + 8.0, kVolTol);
}

// -- 7. Undo / redo ---------------------------------------------------------

TEST_F(SolidOpsTest, UnionThenUndoRestoresOperandsThenRedoRestoresResult) {
    const Id a = makeBoxInstance({0, 0, 0}, {2, 2, 2});
    const Id b = makeBoxInstance({1, 1, 1}, {3, 3, 3});

    kernel.send(SolidOpRequested{SolidOp::Union, {a, b}});
    ASSERT_EQ(geometry->scene().root().children.size(), 1u);
    const Id resultId = geometry->scene().root().children[0].id;
    ASSERT_TRUE(undo->canUndo());

    kernel.send(UndoRequested{});
    ASSERT_EQ(geometry->scene().root().children.size(), 2u);
    EXPECT_NE(geometry->scene().findInstance(kRootDefinitionId, a), nullptr);
    EXPECT_NE(geometry->scene().findInstance(kRootDefinitionId, b), nullptr);
    EXPECT_EQ(geometry->scene().findInstance(kRootDefinitionId, resultId), nullptr);
    EXPECT_TRUE(isSolidDefinition(*defOf(a)));
    EXPECT_TRUE(isSolidDefinition(*defOf(b)));

    kernel.send(RedoRequested{});
    ASSERT_EQ(geometry->scene().root().children.size(), 1u);
    EXPECT_EQ(geometry->scene().findInstance(kRootDefinitionId, a), nullptr);
    EXPECT_EQ(geometry->scene().findInstance(kRootDefinitionId, b), nullptr);
    const Instance& redoneInst = geometry->scene().root().children[0];
    const Definition* redoneDef = geometry->scene().definition(redoneInst.definitionId);
    ASSERT_NE(redoneDef, nullptr);
    EXPECT_TRUE(isSolidDefinition(*redoneDef));
    EXPECT_NEAR(solidVolume(redoneDef->model), 15.0, kVolTol);
}

TEST_F(SolidOpsTest, SplitThenUndoRestoresOperandsThenRedoRestoresAllThreePieces) {
    const Id a = makeBoxInstance({0, 0, 0}, {2, 2, 2});
    const Id b = makeBoxInstance({1, 1, 1}, {3, 3, 3});

    kernel.send(SolidOpRequested{SolidOp::Split, {a, b}});
    ASSERT_EQ(geometry->scene().root().children.size(), 3u);  // multi-Definition creation in one transaction
    ASSERT_TRUE(undo->canUndo());

    kernel.send(UndoRequested{});
    ASSERT_EQ(geometry->scene().root().children.size(), 2u);
    EXPECT_NE(geometry->scene().findInstance(kRootDefinitionId, a), nullptr);
    EXPECT_NE(geometry->scene().findInstance(kRootDefinitionId, b), nullptr);
    EXPECT_TRUE(isSolidDefinition(*defOf(a)));
    EXPECT_TRUE(isSolidDefinition(*defOf(b)));

    kernel.send(RedoRequested{});
    ASSERT_EQ(geometry->scene().root().children.size(), 3u);
    EXPECT_EQ(geometry->scene().findInstance(kRootDefinitionId, a), nullptr);
    EXPECT_EQ(geometry->scene().findInstance(kRootDefinitionId, b), nullptr);
    for (const Instance& inst : geometry->scene().root().children) {
        const Definition* def = geometry->scene().definition(inst.definitionId);
        ASSERT_NE(def, nullptr);
        EXPECT_TRUE(isSolidDefinition(*def));
    }
}

// -- 8. Material carry-over --------------------------------------------------

TEST_F(SolidOpsTest, UnionCarriesFrontMaterialFromASourceFace) {
    const Id a = makeBoxInstance({0, 0, 0}, {2, 2, 2});
    const Id b = makeBoxInstance({1, 1, 1}, {3, 3, 3});
    const Id materialId = materials->create("Red", 1.0, 0.0, 0.0, 1.0);

    const Definition* defA = defOf(a);
    ASSERT_NE(defA, nullptr);
    ASSERT_FALSE(defA->model.faces().empty());
    const Id sourceFaceId = defA->model.faces().begin()->first;
    materials->paint({EntityRef{EntityKind::Face, sourceFaceId}}, materialId);
    ASSERT_NE(materials->assignment(EntityRef{EntityKind::Face, sourceFaceId}), nullptr);

    kernel.send(SolidOpRequested{SolidOp::Union, {a, b}});

    ASSERT_EQ(geometry->scene().root().children.size(), 1u);
    const Instance& inst = geometry->scene().root().children[0];
    const Definition* def = geometry->scene().definition(inst.definitionId);
    ASSERT_NE(def, nullptr);

    int carriedCount = 0;
    for (const auto& [faceId, face] : def->model.faces()) {
        (void)face;
        const auto* assignment = materials->assignment(EntityRef{EntityKind::Face, faceId});
        if (assignment != nullptr && assignment->frontMaterialId == materialId) {
            ++carriedCount;
        }
    }
    EXPECT_GE(carriedCount, 1);
}

// -- Unregistered-agent safety (mirrors group_test.cpp's own
// GroupUnregisteredTest precedent) -----------------------------------------

TEST(SolidOpsUnregisteredTest, SolidOpRequestedWithNoStoresDoesNotCrash) {
    AppKernel kernel;  // GeometryApi/SelectionStore/MaterialRepository deliberately not registered
    kernel.registerCommand<SolidOpRequested, SolidOpCommand>();

    EXPECT_NO_THROW(kernel.send(SolidOpRequested{SolidOp::Union, {1, 2}}));
}

}  // namespace
