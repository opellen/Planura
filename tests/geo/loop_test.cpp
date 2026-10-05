#include <geo/model.h>

#include <cmath>
#include <tuple>
#include <vector>

#include <gtest/gtest.h>

namespace {

using plnr::geo::Id;
using plnr::geo::kInvalidId;
using plnr::geo::Model;
using plnr::geo::Vec3;

// Walks a face's half-edge cycle (via next) from its stored halfEdge,
// returning ids in order; EXPECTs (rather than asserts) if it never closes.
std::vector<Id> faceCycle(const Model& model, Id faceId) {
    const auto* face = model.face(faceId);
    EXPECT_NE(face, nullptr);
    if (face == nullptr) {
        return {};
    }

    std::vector<Id> cycle;
    Id cur = face->halfEdge;
    for (int guard = 0; guard < 64 && cur != kInvalidId; ++guard) {
        cycle.push_back(cur);
        const auto* he = model.halfEdge(cur);
        EXPECT_NE(he, nullptr);
        if (he == nullptr) {
            break;
        }
        cur = he->next;
        if (cur == face->halfEdge) {
            break;
        }
    }
    return cycle;
}

TEST(LoopTest, SquareClosesExactlyOneFaceOnFourthEdge) {
    Model model;

    const auto e1 = model.addEdge(Vec3{0.0, 0.0, 0.0}, Vec3{4.0, 0.0, 0.0});
    ASSERT_TRUE(e1.created);
    EXPECT_TRUE(e1.newFaces.empty());

    const auto e2 = model.addEdge(Vec3{4.0, 0.0, 0.0}, Vec3{4.0, 3.0, 0.0});
    ASSERT_TRUE(e2.created);
    EXPECT_TRUE(e2.newFaces.empty());

    const auto e3 = model.addEdge(Vec3{4.0, 3.0, 0.0}, Vec3{0.0, 3.0, 0.0});
    ASSERT_TRUE(e3.created);
    EXPECT_TRUE(e3.newFaces.empty());
    EXPECT_TRUE(model.faces().empty());

    const auto e4 = model.addEdge(Vec3{0.0, 3.0, 0.0}, Vec3{0.0, 0.0, 0.0});
    ASSERT_TRUE(e4.created);
    ASSERT_EQ(e4.newFaces.size(), 1u);
    EXPECT_EQ(model.faces().size(), 1u);

    const Id faceId = e4.newFaces[0];
    const auto* face = model.face(faceId);
    ASSERT_NE(face, nullptr);

    // Normal must be a unit vector aligned with +/-Z (square lies in XY).
    EXPECT_NEAR(std::fabs(face->normal.z), 1.0, 1e-9);
    EXPECT_NEAR(face->normal.x, 0.0, 1e-9);
    EXPECT_NEAR(face->normal.y, 0.0, 1e-9);

    // Exactly 4 of the 8 half-edges belong to the face, chained into a
    // 4-cycle; the other 4 remain wire (untouched).
    int claimed = 0;
    int wire = 0;
    for (const auto& [id, he] : model.halfEdges()) {
        if (he.face == faceId) {
            ++claimed;
        } else {
            EXPECT_EQ(he.face, kInvalidId);
            EXPECT_EQ(he.next, kInvalidId);
            EXPECT_EQ(he.prev, kInvalidId);
            ++wire;
        }
    }
    EXPECT_EQ(claimed, 4);
    EXPECT_EQ(wire, 4);

    const std::vector<Id> cycle = faceCycle(model, faceId);
    ASSERT_EQ(cycle.size(), 4u);
    for (Id heId : cycle) {
        const auto* he = model.halfEdge(heId);
        ASSERT_NE(he, nullptr);
        EXPECT_EQ(he->face, faceId);
        const auto* next = model.halfEdge(he->next);
        ASSERT_NE(next, nullptr);
        EXPECT_EQ(next->prev, heId);
        // Cycle must actually connect: this half-edge's destination (its
        // twin's origin) is the next half-edge's origin.
        const auto* twin = model.halfEdge(he->twin);
        ASSERT_NE(twin, nullptr);
        EXPECT_EQ(twin->origin, next->origin);
    }
}

TEST(LoopTest, TriangleClosesFaceOnThirdEdge) {
    Model model;

    ASSERT_TRUE(model.addEdge(Vec3{0.0, 0.0, 0.0}, Vec3{2.0, 0.0, 0.0}).created);
    ASSERT_TRUE(model.addEdge(Vec3{2.0, 0.0, 0.0}, Vec3{1.0, 2.0, 0.0}).created);
    EXPECT_TRUE(model.faces().empty());

    const auto e3 = model.addEdge(Vec3{1.0, 2.0, 0.0}, Vec3{0.0, 0.0, 0.0});
    ASSERT_TRUE(e3.created);
    ASSERT_EQ(e3.newFaces.size(), 1u);
    EXPECT_EQ(model.faces().size(), 1u);
}

TEST(LoopTest, ConcaveLShapeAcceptsSimplePolygon) {
    Model model;

    // L-shape: (0,0) (3,0) (3,1) (1,1) (1,2) (0,2), z = 0.
    const Vec3 p0{0.0, 0.0, 0.0};
    const Vec3 p1{3.0, 0.0, 0.0};
    const Vec3 p2{3.0, 1.0, 0.0};
    const Vec3 p3{1.0, 1.0, 0.0};
    const Vec3 p4{1.0, 2.0, 0.0};
    const Vec3 p5{0.0, 2.0, 0.0};

    ASSERT_TRUE(model.addEdge(p0, p1).created);
    ASSERT_TRUE(model.addEdge(p1, p2).created);
    ASSERT_TRUE(model.addEdge(p2, p3).created);
    ASSERT_TRUE(model.addEdge(p3, p4).created);
    ASSERT_TRUE(model.addEdge(p4, p5).created);
    EXPECT_TRUE(model.faces().empty());

    const auto closing = model.addEdge(p5, p0);
    ASSERT_TRUE(closing.created);
    ASSERT_EQ(closing.newFaces.size(), 1u);
    EXPECT_EQ(model.faces().size(), 1u);
}

TEST(LoopTest, NonCoplanarQuadCreatesNoFace) {
    Model model;

    const Vec3 p0{0.0, 0.0, 0.0};
    const Vec3 p1{1.0, 0.0, 0.0};
    const Vec3 p2{1.0, 1.0, 1.0};  // out of the z=0 plane
    const Vec3 p3{0.0, 1.0, 0.0};

    ASSERT_TRUE(model.addEdge(p0, p1).created);
    ASSERT_TRUE(model.addEdge(p1, p2).created);
    ASSERT_TRUE(model.addEdge(p2, p3).created);

    const auto closing = model.addEdge(p3, p0);
    ASSERT_TRUE(closing.created);
    EXPECT_TRUE(closing.newFaces.empty());
    EXPECT_TRUE(model.faces().empty());
}

TEST(LoopTest, SelfIntersectingBowtieCreatesNoFace) {
    Model model;

    const Vec3 p0{0.0, 0.0, 0.0};
    const Vec3 p1{2.0, 2.0, 0.0};
    const Vec3 p2{2.0, 0.0, 0.0};
    const Vec3 p3{0.0, 2.0, 0.0};

    ASSERT_TRUE(model.addEdge(p0, p1).created);
    ASSERT_TRUE(model.addEdge(p1, p2).created);
    ASSERT_TRUE(model.addEdge(p2, p3).created);

    const auto closing = model.addEdge(p3, p0);
    ASSERT_TRUE(closing.created);
    EXPECT_TRUE(closing.newFaces.empty());
    EXPECT_TRUE(model.faces().empty());
}

TEST(LoopTest, ReaddingExistingEdgeIsNoOpEvenWithFacePresent) {
    Model model;

    const Vec3 p0{0.0, 0.0, 0.0};
    const Vec3 p1{4.0, 0.0, 0.0};
    const Vec3 p2{4.0, 3.0, 0.0};
    const Vec3 p3{0.0, 3.0, 0.0};

    ASSERT_TRUE(model.addEdge(p0, p1).created);
    ASSERT_TRUE(model.addEdge(p1, p2).created);
    ASSERT_TRUE(model.addEdge(p2, p3).created);
    const auto e4 = model.addEdge(p3, p0);
    ASSERT_TRUE(e4.created);
    ASSERT_EQ(model.faces().size(), 1u);

    const auto sizeBefore = std::make_tuple(model.vertices().size(), model.edges().size(),
                                             model.halfEdges().size(), model.faces().size());

    const auto redo = model.addEdge(p0, p1);
    EXPECT_FALSE(redo.created);
    EXPECT_TRUE(redo.newFaces.empty());

    const auto sizeAfter = std::make_tuple(model.vertices().size(), model.edges().size(),
                                            model.halfEdges().size(), model.faces().size());
    EXPECT_EQ(sizeBefore, sizeAfter);
}

TEST(LoopTest, RemoveEdgeDissolvesFaceAndRestoresWireHalves) {
    Model model;

    const Vec3 p0{0.0, 0.0, 0.0};
    const Vec3 p1{4.0, 0.0, 0.0};
    const Vec3 p2{4.0, 3.0, 0.0};
    const Vec3 p3{0.0, 3.0, 0.0};

    const auto e1 = model.addEdge(p0, p1);
    const auto e2 = model.addEdge(p1, p2);
    const auto e3 = model.addEdge(p2, p3);
    const auto e4 = model.addEdge(p3, p0);
    ASSERT_TRUE(e1.created && e2.created && e3.created && e4.created);
    ASSERT_EQ(e4.newFaces.size(), 1u);
    ASSERT_EQ(model.faces().size(), 1u);

    ASSERT_TRUE(model.removeEdge(e1.edge));

    EXPECT_TRUE(model.faces().empty());
    EXPECT_EQ(model.edges().size(), 3u);

    for (const auto& [id, he] : model.halfEdges()) {
        EXPECT_EQ(he.face, kInvalidId);
        EXPECT_EQ(he.next, kInvalidId);
        EXPECT_EQ(he.prev, kInvalidId);
    }
}

TEST(LoopTest, TwoSeparateSquaresInDifferentPlanesEachGetOwnFace) {
    Model model;

    // Square 1 in the XY plane (z = 0).
    ASSERT_TRUE(model.addEdge(Vec3{0.0, 0.0, 0.0}, Vec3{1.0, 0.0, 0.0}).created);
    ASSERT_TRUE(model.addEdge(Vec3{1.0, 0.0, 0.0}, Vec3{1.0, 1.0, 0.0}).created);
    ASSERT_TRUE(model.addEdge(Vec3{1.0, 1.0, 0.0}, Vec3{0.0, 1.0, 0.0}).created);
    const auto close1 = model.addEdge(Vec3{0.0, 1.0, 0.0}, Vec3{0.0, 0.0, 0.0});
    ASSERT_TRUE(close1.created);
    ASSERT_EQ(close1.newFaces.size(), 1u);

    // Square 2 in the XZ plane (y = 10), fully disconnected from square 1.
    ASSERT_TRUE(model.addEdge(Vec3{0.0, 10.0, 0.0}, Vec3{1.0, 10.0, 0.0}).created);
    ASSERT_TRUE(model.addEdge(Vec3{1.0, 10.0, 0.0}, Vec3{1.0, 10.0, 1.0}).created);
    ASSERT_TRUE(model.addEdge(Vec3{1.0, 10.0, 1.0}, Vec3{0.0, 10.0, 1.0}).created);
    const auto close2 = model.addEdge(Vec3{0.0, 10.0, 1.0}, Vec3{0.0, 10.0, 0.0});
    ASSERT_TRUE(close2.created);
    ASSERT_EQ(close2.newFaces.size(), 1u);

    EXPECT_EQ(model.faces().size(), 2u);
    EXPECT_NE(close1.newFaces[0], close2.newFaces[0]);
}

}  // namespace
