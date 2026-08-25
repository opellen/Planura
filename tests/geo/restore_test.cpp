#include <geo/model.h>
#include <geo/scene.h>

#include <algorithm>
#include <vector>

#include <gtest/gtest.h>

namespace {

using plnr::geo::AddEdgeResult;
using plnr::geo::almostEqual;
using plnr::geo::Definition;
using plnr::geo::Face;
using plnr::geo::Id;
using plnr::geo::Instance;
using plnr::geo::kInvalidId;
using plnr::geo::kMergeTol;
using plnr::geo::kRootDefinitionId;
using plnr::geo::Model;
using plnr::geo::Scene;
using plnr::geo::Transform;
using plnr::geo::Vec3;

// -- Shared helpers -----------------------------------------------------

// Walks a face's half-edge next-cycle starting at its own recorded
// Face::halfEdge; asserts it returns to that start after loop.size() steps.
void expectValidNextCycle(const Model& model, Id faceId) {
    const Face* face = model.face(faceId);
    ASSERT_NE(face, nullptr);
    const std::vector<Id> loop = model.faceVertexLoop(faceId);
    ASSERT_GE(loop.size(), 3u);

    Id cur = face->halfEdge;
    for (std::size_t i = 0; i < loop.size(); ++i) {
        const auto* he = model.halfEdge(cur);
        ASSERT_NE(he, nullptr);
        EXPECT_EQ(he->face, faceId);
        cur = he->next;
    }
    EXPECT_EQ(cur, face->halfEdge) << "next-cycle did not return to face's own start half-edge";
}

// True if b is some rotation of a -- same cycle, same winding, possibly a
// different starting vertex. Used to compare a restored face's vertex loop
// against the original's.
bool isRotation(const std::vector<Id>& a, const std::vector<Id>& b) {
    if (a.size() != b.size()) {
        return false;
    }
    const std::size_t n = a.size();
    if (n == 0) {
        return true;
    }
    for (std::size_t shift = 0; shift < n; ++shift) {
        bool match = true;
        for (std::size_t i = 0; i < n; ++i) {
            if (a[i] != b[(i + shift) % n]) {
                match = false;
                break;
            }
        }
        if (match) {
            return true;
        }
    }
    return false;
}

// Every id currently used by any of the model's four record maps -- the max
// of these + 1 is exactly what a real loader would pass to restoreNextId
// (see Model::restoreNextId's own header comment).
Id maxIdInUse(const Model& model) {
    Id maxId = kInvalidId;
    for (const auto& [id, v] : model.vertices()) {
        (void)v;
        maxId = std::max(maxId, id);
    }
    for (const auto& [id, e] : model.edges()) {
        (void)e;
        maxId = std::max(maxId, id);
    }
    for (const auto& [id, h] : model.halfEdges()) {
        (void)h;
        maxId = std::max(maxId, id);
    }
    for (const auto& [id, f] : model.faces()) {
        (void)f;
        maxId = std::max(maxId, id);
    }
    return maxId;
}

// -- Model::restoreNextId ordering contract --------------------------------

TEST(RestoreTest, ModelRestoreNextIdRejectsLoweringAndGatesLaterRestoreCalls) {
    Model model;
    EXPECT_TRUE(model.restoreNextId(100));
    // Lowering is rejected -- no mutation to nextId_.
    EXPECT_FALSE(model.restoreNextId(50));
    // restoreVertex at/after the reserved nextId_ is rejected (id must be <
    // nextId_ -- restoreNextId must cover it first).
    EXPECT_FALSE(model.restoreVertex(100, Vec3{0.0, 0.0, 0.0}));
    EXPECT_FALSE(model.restoreVertex(150, Vec3{0.0, 0.0, 0.0}));
    // A properly-reserved id restores fine.
    EXPECT_TRUE(model.restoreVertex(99, Vec3{1.0, 2.0, 3.0}));
    EXPECT_EQ(model.vertices().size(), 1u);
}

TEST(RestoreTest, ModelRestoreRejectsIdReuseAcrossRecordKinds) {
    Model model;
    ASSERT_TRUE(model.restoreNextId(100));
    ASSERT_TRUE(model.restoreVertex(1, Vec3{0.0, 0.0, 0.0}));
    ASSERT_TRUE(model.restoreVertex(2, Vec3{1.0, 0.0, 0.0}));
    ASSERT_TRUE(model.restoreEdge(3, 1, 2));  // half-edges allocId()'d as 100, 101

    // Every id already used by ANY record kind is rejected, whichever
    // restore* call is attempted next.
    EXPECT_FALSE(model.restoreVertex(1, Vec3{5.0, 5.0, 5.0}));  // vertex id reused
    EXPECT_FALSE(model.restoreVertex(3, Vec3{5.0, 5.0, 5.0}));  // edge id reused as vertex
    EXPECT_FALSE(model.restoreEdge(1, 1, 2));                   // vertex id reused as edge
    EXPECT_FALSE(model.restoreEdge(2, 1, 2));                   // vertex id reused as edge (other vertex)
    EXPECT_EQ(model.vertices().size(), 2u);
    EXPECT_EQ(model.edges().size(), 1u);

    // kInvalidId is always rejected.
    EXPECT_FALSE(model.restoreVertex(kInvalidId, Vec3{}));

    // The half-edge ids restoreEdge allocated (100, 101) are also blocked as
    // a future restorable id, once a later restoreNextId reserves them.
    ASSERT_TRUE(model.restoreNextId(102));
    EXPECT_FALSE(model.restoreVertex(100, Vec3{9.0, 9.0, 9.0}));  // half-edge id reused as vertex
    EXPECT_EQ(model.vertices().size(), 2u);
}

TEST(RestoreTest, ModelRestoreEdgeRejectsSelfLoopAndUnknownVertices) {
    Model model;
    ASSERT_TRUE(model.restoreNextId(10));
    ASSERT_TRUE(model.restoreVertex(1, Vec3{0.0, 0.0, 0.0}));

    EXPECT_FALSE(model.restoreEdge(2, 1, 1));    // v0 == v1
    EXPECT_FALSE(model.restoreEdge(2, 1, 999));  // v1 unknown
    EXPECT_FALSE(model.restoreEdge(2, 999, 1));  // v0 unknown
    EXPECT_TRUE(model.edges().empty());
    EXPECT_TRUE(model.halfEdges().empty());
}

// -- Model round-trip: normal build -> dump -> restore ---------------------

// Builds a unit square face A-B-C-D, a disconnected wire edge, and a second
// triangle D-G-A sharing edge D-A but claiming its OTHER half-edge direction
// (exercises twin claiming from both sides). Round-trips via dump + restore.
TEST(RestoreTest, ModelRoundTripPreservesIdsPositionsLoopsNormalsAndTopology) {
    Model original;

    const Vec3 a{0.0, 0.0, 0.0};
    const Vec3 b{1.0, 0.0, 0.0};
    const Vec3 c{1.0, 1.0, 0.0};
    const Vec3 d{0.0, 1.0, 0.0};
    original.addEdge(a, b);
    original.addEdge(b, c);
    original.addEdge(c, d);
    const AddEdgeResult closing = original.addEdge(d, a);
    ASSERT_EQ(closing.newFaces.size(), 1u);  // face 1: the square itself

    const Vec3 e{5.0, 5.0, 0.0};
    const Vec3 f{6.0, 5.0, 0.0};
    const AddEdgeResult wire = original.addEdge(e, f);
    ASSERT_TRUE(wire.created);
    ASSERT_TRUE(wire.newFaces.empty());

    // Face 2: triangle D-G-A sharing edge D-A with face 1, claiming the
    // opposite winding. Normal is the Newell normal for [d,g,a] order --
    // resolveLoopWinding's twin-fallback sign flip keeps it consistent either way.
    const Vec3 g{-1.0, 0.5, 0.0};
    original.addEdge(d, g, /*detectFaces=*/false);
    original.addEdge(g, a, /*detectFaces=*/false);
    const Id vD = original.findVertex(d)->id;
    const Id vG = original.findVertex(g)->id;
    const Id vA = original.findVertex(a)->id;
    const Id face2 = original.addFaceOnLoop({vD, vG, vA}, Vec3{0.0, 0.0, 1.0});
    ASSERT_NE(face2, kInvalidId);

    // --- Dump the original model's records. ---
    struct VertexRecord {
        Id id;
        Vec3 pos;
    };
    struct EdgeRecord {
        Id id;
        Id v0;
        Id v1;
    };
    struct FaceRecord {
        Id id;
        std::vector<Id> loop;
    };

    std::vector<VertexRecord> vertexRecords;
    for (const auto& [id, v] : original.vertices()) {
        vertexRecords.push_back({id, v.pos});
    }
    std::vector<EdgeRecord> edgeRecords;
    for (const auto& [id, edge] : original.edges()) {
        const Id v0 = original.halfEdge(edge.halfEdges[0])->origin;
        const Id v1 = original.halfEdge(edge.halfEdges[1])->origin;
        edgeRecords.push_back({id, v0, v1});
    }
    std::vector<FaceRecord> faceRecords;
    for (const auto& [id, face] : original.faces()) {
        (void)face;
        faceRecords.push_back({id, original.faceVertexLoop(id)});
    }
    const Id nextId = maxIdInUse(original) + 1;

    // --- Rebuild a fresh model via restore APIs, in the documented order. ---
    Model restored;
    ASSERT_TRUE(restored.restoreNextId(nextId));
    for (const auto& rec : vertexRecords) {
        ASSERT_TRUE(restored.restoreVertex(rec.id, rec.pos));
    }
    for (const auto& rec : edgeRecords) {
        ASSERT_TRUE(restored.restoreEdge(rec.id, rec.v0, rec.v1));
    }
    for (const auto& rec : faceRecords) {
        ASSERT_TRUE(restored.restoreFace(rec.id, rec.loop));
    }

    // --- Identical vertex/edge/face id sets and positions. ---
    ASSERT_EQ(restored.vertices().size(), original.vertices().size());
    for (const auto& [id, v] : original.vertices()) {
        const auto* rv = restored.vertex(id);
        ASSERT_NE(rv, nullptr);
        EXPECT_TRUE(almostEqual(rv->pos, v.pos));
    }
    ASSERT_EQ(restored.edges().size(), original.edges().size());
    for (const auto& [id, e] : original.edges()) {
        (void)e;
        EXPECT_NE(restored.edge(id), nullptr);
    }
    ASSERT_EQ(restored.faces().size(), original.faces().size());

    // --- Identical face vertex loops (same rotation-invariant cycle and
    // winding) and normals. ---
    for (const auto& [id, face] : original.faces()) {
        const std::vector<Id> originalLoop = original.faceVertexLoop(id);
        const std::vector<Id> restoredLoop = restored.faceVertexLoop(id);
        EXPECT_TRUE(isRotation(originalLoop, restoredLoop)) << "face " << id << " loop winding/rotation mismatch";

        const auto* restoredFace = restored.face(id);
        ASSERT_NE(restoredFace, nullptr);
        EXPECT_TRUE(almostEqual(restoredFace->normal, face.normal, kMergeTol));

        expectValidNextCycle(restored, id);
    }

    // --- The wire edge's half-edges still have kInvalidId face. ---
    const auto* wireEdge = restored.edge(wire.edge);
    ASSERT_NE(wireEdge, nullptr);
    for (Id heId : wireEdge->halfEdges) {
        EXPECT_EQ(restored.halfEdge(heId)->face, kInvalidId);
    }

    // --- Twin/next/prev invariants across every half-edge. ---
    for (const auto& [id, he] : restored.halfEdges()) {
        const auto* twin = restored.halfEdge(he.twin);
        ASSERT_NE(twin, nullptr);
        EXPECT_EQ(twin->twin, id);
        if (he.face == kInvalidId) {
            EXPECT_EQ(he.next, kInvalidId);
            EXPECT_EQ(he.prev, kInvalidId);
        } else {
            const auto* next = restored.halfEdge(he.next);
            const auto* prev = restored.halfEdge(he.prev);
            ASSERT_NE(next, nullptr);
            ASSERT_NE(prev, nullptr);
            EXPECT_EQ(next->prev, id);
            EXPECT_EQ(prev->next, id);
            EXPECT_EQ(next->face, he.face);
            EXPECT_EQ(prev->face, he.face);
        }
    }

    // --- Post-restore addEdge still works and allocates ids above
    // everything restored. ---
    const Id maxRestoredId = maxIdInUse(restored);
    const AddEdgeResult postRestore = restored.addEdge(Vec3{20.0, 20.0, 0.0}, Vec3{21.0, 20.0, 0.0});
    ASSERT_TRUE(postRestore.created);
    EXPECT_GT(postRestore.edge, maxRestoredId);
}

// -- restoreFace strict-winding failures ------------------------------------

TEST(RestoreTest, ModelRestoreFaceStrictWindingRejectsAlreadyClaimedHalfEdges) {
    Model model;
    ASSERT_TRUE(model.restoreNextId(1000));
    ASSERT_TRUE(model.restoreVertex(1, Vec3{0.0, 0.0, 0.0}));
    ASSERT_TRUE(model.restoreVertex(2, Vec3{1.0, 0.0, 0.0}));
    ASSERT_TRUE(model.restoreVertex(3, Vec3{0.0, 1.0, 0.0}));
    ASSERT_TRUE(model.restoreEdge(4, 1, 2));
    ASSERT_TRUE(model.restoreEdge(5, 2, 3));
    ASSERT_TRUE(model.restoreEdge(6, 3, 1));
    ASSERT_TRUE(model.restoreFace(7, {1, 2, 3}));

    const std::size_t facesBefore = model.faces().size();
    const std::size_t halfEdgesBefore = model.halfEdges().size();

    // Face 8 restores THE SAME loop in the SAME direction -- every half-edge
    // it needs is already claimed by face 7, and restoreFace never falls
    // back to the twin side, so this must fail outright with no mutation.
    EXPECT_FALSE(model.restoreFace(8, {1, 2, 3}));
    EXPECT_EQ(model.face(8), nullptr);
    EXPECT_EQ(model.faces().size(), facesBefore);
    EXPECT_EQ(model.halfEdges().size(), halfEdgesBefore);

    // The reversed direction is a DIFFERENT (still free) set of half-edges --
    // restoreFace only tries the loop's stated direction, unlike addFaceOnLoop,
    // which claims the free twin side here to prove it was untouched.
    EXPECT_NE(model.addFaceOnLoop({3, 2, 1}, Vec3{0.0, 0.0, -1.0}), kInvalidId);
}

TEST(RestoreTest, ModelRestoreFaceRejectsMissingEdgeWithoutPartiallyClaimingTheRest) {
    Model model;
    ASSERT_TRUE(model.restoreNextId(1000));
    ASSERT_TRUE(model.restoreVertex(1, Vec3{0.0, 0.0, 0.0}));
    ASSERT_TRUE(model.restoreVertex(2, Vec3{1.0, 0.0, 0.0}));
    ASSERT_TRUE(model.restoreVertex(3, Vec3{0.0, 1.0, 0.0}));
    // Only two of the loop's three edges exist -- vertex 3 -> vertex 1 is
    // missing.
    ASSERT_TRUE(model.restoreEdge(4, 1, 2));
    ASSERT_TRUE(model.restoreEdge(5, 2, 3));

    EXPECT_FALSE(model.restoreFace(6, {1, 2, 3}));
    EXPECT_EQ(model.face(6), nullptr);
    EXPECT_TRUE(model.faces().empty());

    // No partial mutation -- the half-edges backing the two edges that DO
    // exist must still be wire (unclaimed), proving resolveLoopWinding
    // validates every pair before claiming any of them.
    for (const auto& [id, edge] : model.edges()) {
        (void)id;
        for (Id heId : edge.halfEdges) {
            EXPECT_EQ(model.halfEdge(heId)->face, kInvalidId);
        }
    }

    // Unknown vertex in the loop is rejected the same way.
    EXPECT_FALSE(model.restoreFace(6, {1, 2, 999}));

    // Loop too short.
    EXPECT_FALSE(model.restoreFace(6, {1, 2}));
}

// -- Scene::restoreNextId ordering contract ---------------------------------

TEST(RestoreTest, SceneRestoreNextIdRejectsLoweringAndGatesLaterRestoreCalls) {
    Scene scene;
    EXPECT_TRUE(scene.restoreNextId(50));
    EXPECT_FALSE(scene.restoreNextId(10));  // lowering rejected

    EXPECT_EQ(scene.restoreDefinition(50, "Group", true), nullptr);  // id >= nextId_
    Definition* def = scene.restoreDefinition(49, "Group", true);
    ASSERT_NE(def, nullptr);
    EXPECT_EQ(def->id, 49u);
    EXPECT_EQ(def->name, "Group");
    EXPECT_TRUE(def->isGroup);
}

// -- Scene round-trip: root + one group definition + two instances ---------

TEST(RestoreTest, SceneRoundTripPreservesDefinitionAndInstanceIds) {
    Scene original;
    const Id groupDefId = original.createDefinition("MyGroup", /*isGroup=*/true);
    const Transform xf1 = Transform::translation(Vec3{1.0, 2.0, 3.0});
    const Transform xf2 = Transform::translation(Vec3{-4.0, 0.0, 5.0});
    const Id inst1 = original.addInstance(kRootDefinitionId, groupDefId, xf1, "Instance A");
    const Id inst2 = original.addInstance(kRootDefinitionId, groupDefId, xf2, "Instance B");
    ASSERT_NE(groupDefId, kInvalidId);
    ASSERT_NE(inst1, kInvalidId);
    ASSERT_NE(inst2, kInvalidId);

    const Id nextId = std::max({groupDefId, inst1, inst2}) + 1;

    Scene restored;
    ASSERT_TRUE(restored.restoreNextId(nextId));
    Definition* restoredDef = restored.restoreDefinition(groupDefId, "MyGroup", true);
    ASSERT_NE(restoredDef, nullptr);
    EXPECT_EQ(restoredDef->id, groupDefId);
    EXPECT_EQ(restoredDef->name, "MyGroup");
    EXPECT_TRUE(restoredDef->isGroup);

    ASSERT_TRUE(restored.restoreInstance(kRootDefinitionId, inst1, groupDefId, xf1, "Instance A"));
    ASSERT_TRUE(restored.restoreInstance(kRootDefinitionId, inst2, groupDefId, xf2, "Instance B"));

    const Instance* restoredInst1 = restored.findInstance(kRootDefinitionId, inst1);
    const Instance* restoredInst2 = restored.findInstance(kRootDefinitionId, inst2);
    ASSERT_NE(restoredInst1, nullptr);
    ASSERT_NE(restoredInst2, nullptr);
    EXPECT_EQ(restoredInst1->definitionId, groupDefId);
    EXPECT_EQ(restoredInst1->name, "Instance A");
    EXPECT_TRUE(restoredInst1->transform.almostEqual(xf1));
    EXPECT_EQ(restoredInst2->definitionId, groupDefId);
    EXPECT_EQ(restoredInst2->name, "Instance B");
    EXPECT_TRUE(restoredInst2->transform.almostEqual(xf2));

    // Root is untouched apart from the two restored instances.
    EXPECT_EQ(restored.root().id, kRootDefinitionId);
    EXPECT_EQ(restored.root().children.size(), 2u);

    // -- Reject id reuse between the definition and instance id spaces. --
    // An instance id can't be reused for a new definition...
    EXPECT_EQ(restored.restoreDefinition(inst1, "Clash", false), nullptr);
    // ...and a definition id can't be reused for a new instance.
    EXPECT_FALSE(restored.restoreInstance(kRootDefinitionId, groupDefId, groupDefId, Transform::identity(), "Clash"));
    EXPECT_EQ(restored.root().children.size(), 2u);  // neither clash mutated anything

    // Ordinary (non-restore) allocation still continues above everything
    // restored.
    const Id postRestoreDef = restored.createDefinition("New", false);
    EXPECT_GT(postRestoreDef, std::max({groupDefId, inst1, inst2}));
}

}  // namespace
