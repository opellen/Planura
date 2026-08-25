#include <geo/pick.h>

#include <gtest/gtest.h>

namespace {

using plnr::geo::almostEqual;
using plnr::geo::EntityKind;
using plnr::geo::Model;
using plnr::geo::PickKind;
using plnr::geo::PickOptions;
using plnr::geo::PickResult;
using plnr::geo::Ray;
using plnr::geo::Vec3;
using plnr::geo::Vertex;

TEST(PickTest, VertexHitWithinTolReportsDepth) {
    Model model;
    // Second endpoint deliberately off the ray's line -- otherwise it would
    // also qualify as a vertex candidate and, being nearer, win instead.
    ASSERT_TRUE(model.addEdge(Vec3{0.0, 0.0, 0.0}, Vec3{1.0, 1.0, 0.0}).created);
    const Vertex* v0 = model.findVertex(Vec3{0.0, 0.0, 0.0});
    ASSERT_NE(v0, nullptr);

    const Ray ray{Vec3{5.0, 0.0, 0.0}, Vec3{-1.0, 0.0, 0.0}};
    const PickOptions opts{0.05, 0.05};
    const PickResult result = plnr::geo::pick(model, ray, opts);

    EXPECT_EQ(result.kind, PickKind::Vertex);
    EXPECT_EQ(result.id, v0->id);
    EXPECT_NEAR(result.depth, 5.0, 1e-9);
    EXPECT_TRUE(almostEqual(result.point, Vec3{0.0, 0.0, 0.0}, 1e-9));
}

TEST(PickTest, VertexBeatsNearbyEdgeAtSharedEndpoint) {
    Model model;
    const Vec3 p0{0.0, 0.0, 0.0};
    const Vec3 p1{4.0, 0.0, 0.0};
    const Vec3 p2{4.0, 3.0, 0.0};
    const Vec3 p3{0.0, 3.0, 0.0};
    ASSERT_TRUE(model.addEdge(p0, p1).created);
    ASSERT_TRUE(model.addEdge(p1, p2).created);
    ASSERT_TRUE(model.addEdge(p2, p3).created);
    ASSERT_TRUE(model.addEdge(p3, p0).created);

    const Vertex* v0 = model.findVertex(p0);
    ASSERT_NE(v0, nullptr);

    // Ray comes straight down onto the shared corner p0, which is also the
    // nearest point of both edges meeting there -- vertex priority must win.
    const Ray ray{Vec3{0.0, 0.0, 5.0}, Vec3{0.0, 0.0, -1.0}};
    const PickOptions opts{0.1, 0.1};
    const PickResult result = plnr::geo::pick(model, ray, opts);

    EXPECT_EQ(result.kind, PickKind::Vertex);
    EXPECT_EQ(result.id, v0->id);
}

TEST(PickTest, EdgeHitAtMidSegmentReportsClosestPointAndDepth) {
    Model model;
    const auto e = model.addEdge(Vec3{0.0, 0.0, 0.0}, Vec3{4.0, 0.0, 0.0});
    ASSERT_TRUE(e.created);

    const Ray ray{Vec3{2.0, 5.0, 0.0}, Vec3{0.0, -1.0, 0.0}};
    const PickOptions opts{0.1, 0.5};
    const PickResult result = plnr::geo::pick(model, ray, opts);

    EXPECT_EQ(result.kind, PickKind::Edge);
    EXPECT_EQ(result.id, e.edge);
    EXPECT_NEAR(result.depth, 5.0, 1e-9);
    EXPECT_TRUE(almostEqual(result.point, Vec3{2.0, 0.0, 0.0}, 1e-9));
}

TEST(PickTest, EdgeClosestPointClampsToSegmentEndpoint) {
    Model model;
    const auto e = model.addEdge(Vec3{0.0, 0.0, 0.0}, Vec3{4.0, 0.0, 0.0});
    ASSERT_TRUE(e.created);

    // Ray passes the infinite line extended beyond the segment (x = 6);
    // the closest point on the SEGMENT must clamp to the endpoint (4,0,0).
    const Ray ray{Vec3{6.0, 5.0, 0.0}, Vec3{0.0, -1.0, 0.0}};
    const PickOptions opts{0.5, 2.5};
    const PickResult result = plnr::geo::pick(model, ray, opts);

    EXPECT_EQ(result.kind, PickKind::Edge);
    EXPECT_EQ(result.id, e.edge);
    EXPECT_NEAR(result.depth, 5.0, 1e-9);
    EXPECT_TRUE(almostEqual(result.point, Vec3{4.0, 0.0, 0.0}, 1e-9));
}

TEST(PickTest, FaceHitInsideSquareReportsDepthAndPoint) {
    Model model;
    const Vec3 p0{0.0, 0.0, 0.0};
    const Vec3 p1{4.0, 0.0, 0.0};
    const Vec3 p2{4.0, 3.0, 0.0};
    const Vec3 p3{0.0, 3.0, 0.0};
    ASSERT_TRUE(model.addEdge(p0, p1).created);
    ASSERT_TRUE(model.addEdge(p1, p2).created);
    ASSERT_TRUE(model.addEdge(p2, p3).created);
    const auto closing = model.addEdge(p3, p0);
    ASSERT_TRUE(closing.created);
    ASSERT_EQ(closing.newFaces.size(), 1u);

    const Ray ray{Vec3{2.0, 1.5, 5.0}, Vec3{0.0, 0.0, -1.0}};
    const PickOptions opts{0.1, 0.1};
    const PickResult result = plnr::geo::pick(model, ray, opts);

    EXPECT_EQ(result.kind, PickKind::Face);
    EXPECT_EQ(result.id, closing.newFaces[0]);
    EXPECT_NEAR(result.depth, 5.0, 1e-9);
    EXPECT_TRUE(almostEqual(result.point, Vec3{2.0, 1.5, 0.0}, 1e-9));
}

TEST(PickTest, ConcaveLShapeMissesInNotchAndHitsInSolidPart) {
    Model model;
    // L-shape occupying the bottom strip (0<=x<=3, 0<=y<=1) plus the left
    // column (0<=x<=1, 1<=y<=2); (2, 1.5) sits in the missing notch.
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
    const auto closing = model.addEdge(p5, p0);
    ASSERT_TRUE(closing.created);
    ASSERT_EQ(closing.newFaces.size(), 1u);

    const PickOptions opts{0.2, 0.2};

    const Ray notchRay{Vec3{2.0, 1.5, 5.0}, Vec3{0.0, 0.0, -1.0}};
    const PickResult notchResult = plnr::geo::pick(model, notchRay, opts);
    EXPECT_EQ(notchResult.kind, PickKind::None);

    const Ray solidRay{Vec3{0.5, 0.5, 5.0}, Vec3{0.0, 0.0, -1.0}};
    const PickResult solidResult = plnr::geo::pick(model, solidRay, opts);
    EXPECT_EQ(solidResult.kind, PickKind::Face);
    EXPECT_EQ(solidResult.id, closing.newFaces[0]);
    EXPECT_TRUE(almostEqual(solidResult.point, Vec3{0.5, 0.5, 0.0}, 1e-9));
}

TEST(PickTest, RayPointingAwayFromGeometryMisses) {
    Model model;
    ASSERT_TRUE(model.addEdge(Vec3{0.0, 0.0, 0.0}, Vec3{1.0, 0.0, 0.0}).created);

    // Origin sits past the geometry along +x, ray points further away (+x):
    // every candidate would need t < 0.
    const Ray ray{Vec3{5.0, 0.0, 0.0}, Vec3{1.0, 0.0, 0.0}};
    const PickOptions opts{0.5, 0.5};
    const PickResult result = plnr::geo::pick(model, ray, opts);

    EXPECT_EQ(result.kind, PickKind::None);
}

TEST(PickTest, NearerFaceWinsOverFaceBehindIt) {
    Model model;

    // Face A: square in the z = 0 plane.
    ASSERT_TRUE(model.addEdge(Vec3{0.0, 0.0, 0.0}, Vec3{4.0, 0.0, 0.0}).created);
    ASSERT_TRUE(model.addEdge(Vec3{4.0, 0.0, 0.0}, Vec3{4.0, 3.0, 0.0}).created);
    ASSERT_TRUE(model.addEdge(Vec3{4.0, 3.0, 0.0}, Vec3{0.0, 3.0, 0.0}).created);
    const auto closeA = model.addEdge(Vec3{0.0, 3.0, 0.0}, Vec3{0.0, 0.0, 0.0});
    ASSERT_TRUE(closeA.created);
    ASSERT_EQ(closeA.newFaces.size(), 1u);

    // Face B: identical square translated to z = 3 (disconnected loop),
    // sitting between the ray origin and face A.
    ASSERT_TRUE(model.addEdge(Vec3{0.0, 0.0, 3.0}, Vec3{4.0, 0.0, 3.0}).created);
    ASSERT_TRUE(model.addEdge(Vec3{4.0, 0.0, 3.0}, Vec3{4.0, 3.0, 3.0}).created);
    ASSERT_TRUE(model.addEdge(Vec3{4.0, 3.0, 3.0}, Vec3{0.0, 3.0, 3.0}).created);
    const auto closeB = model.addEdge(Vec3{0.0, 3.0, 3.0}, Vec3{0.0, 0.0, 3.0});
    ASSERT_TRUE(closeB.created);
    ASSERT_EQ(closeB.newFaces.size(), 1u);

    const Ray ray{Vec3{2.0, 1.5, 10.0}, Vec3{0.0, 0.0, -1.0}};
    const PickOptions opts{0.2, 0.2};
    const PickResult result = plnr::geo::pick(model, ray, opts);

    EXPECT_EQ(result.kind, PickKind::Face);
    EXPECT_EQ(result.id, closeB.newFaces[0]);
    EXPECT_NEAR(result.depth, 7.0, 1e-9);
}

TEST(PickTest, FilteredOutEdgeFallsThroughToFace) {
    Model model;
    const Vec3 p0{0.0, 0.0, 0.0};
    const Vec3 p1{4.0, 0.0, 0.0};
    const Vec3 p2{4.0, 3.0, 0.0};
    const Vec3 p3{0.0, 3.0, 0.0};
    const auto bottomEdge = model.addEdge(p0, p1);
    ASSERT_TRUE(bottomEdge.created);
    ASSERT_TRUE(model.addEdge(p1, p2).created);
    ASSERT_TRUE(model.addEdge(p2, p3).created);
    const auto closing = model.addEdge(p3, p0);
    ASSERT_TRUE(closing.created);
    ASSERT_EQ(closing.newFaces.size(), 1u);

    // Just off the bottom edge (within edgeTol) but still inside the face:
    // without the filter this is an Edge hit (edges beat faces); filtering it
    // out should fall through to the Face candidate, not PickKind::None.
    const Ray ray{Vec3{2.0, 0.05, 5.0}, Vec3{0.0, 0.0, -1.0}};
    PickOptions opts{0.001, 0.1};
    opts.filter = [edgeId = bottomEdge.edge](EntityKind kind, plnr::geo::Id id) {
        return !(kind == EntityKind::Edge && id == edgeId);
    };

    const PickResult result = plnr::geo::pick(model, ray, opts);

    EXPECT_EQ(result.kind, PickKind::Face);
    EXPECT_EQ(result.id, closing.newFaces[0]);
}

TEST(PickTest, FilteredOutEdgeWithNoFaceBehindItFallsThroughToNone) {
    Model model;
    const auto e = model.addEdge(Vec3{0.0, 0.0, 0.0}, Vec3{4.0, 0.0, 0.0});
    ASSERT_TRUE(e.created);

    const Ray ray{Vec3{2.0, 5.0, 0.0}, Vec3{0.0, -1.0, 0.0}};
    PickOptions opts{0.1, 0.5};
    opts.filter = [edgeId = e.edge](EntityKind kind, plnr::geo::Id id) {
        return !(kind == EntityKind::Edge && id == edgeId);
    };

    const PickResult result = plnr::geo::pick(model, ray, opts);

    EXPECT_EQ(result.kind, PickKind::None);
}

}  // namespace
