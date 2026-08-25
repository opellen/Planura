#include <geo/infer.h>

#include <optional>
#include <utility>

#include <gtest/gtest.h>

namespace {

using plnr::geo::almostEqual;
using plnr::geo::AxisLock;
using plnr::geo::GuideLineData;
using plnr::geo::GuidePointData;
using plnr::geo::Id;
using plnr::geo::Inference;
using plnr::geo::InferenceContext;
using plnr::geo::InferenceKind;
using plnr::geo::Model;
using plnr::geo::normalized;
using plnr::geo::PickOptions;
using plnr::geo::Ray;
using plnr::geo::Vec3;
using plnr::geo::Vertex;

TEST(InferTest, EndpointBeatsAxisBeatsGround) {
    Model model;
    ASSERT_TRUE(model.addEdge(Vec3{0.0, 0.0, 0.0}, Vec3{1.0, 0.0, 0.0}).created);
    const Vertex* v0 = model.findVertex(Vec3{0.0, 0.0, 0.0});
    ASSERT_NE(v0, nullptr);

    // Ray goes straight through the vertex, which also lies on the ground
    // plane and the locked X axis -- only priority order can decide, and
    // Endpoint must win.
    InferenceContext ctx;
    ctx.axisLock = AxisLock{Vec3{0.0, 0.0, 0.0}, Vec3{1.0, 0.0, 0.0}};
    ctx.tols = PickOptions{0.1, 0.1};

    const Ray ray{Vec3{0.0, 0.0, 5.0}, Vec3{0.0, 0.0, -1.0}};
    const Inference result = plnr::geo::infer(model, ray, ctx);

    EXPECT_EQ(result.kind, InferenceKind::Endpoint);
    EXPECT_EQ(result.refId, v0->id);
    EXPECT_TRUE(almostEqual(result.pos, Vec3{0.0, 0.0, 0.0}, 1e-9));
}

TEST(InferTest, AxisLockProjectsRayOntoLockedXAxis) {
    Model model;  // empty -- no vertex can short-circuit into Endpoint

    InferenceContext ctx;
    ctx.axisLock = AxisLock{Vec3{0.0, 0.0, 0.0}, Vec3{1.0, 0.0, 0.0}};
    ctx.tols = PickOptions{0.05, 0.05};

    const Ray ray{Vec3{3.0, 0.0, 5.0}, Vec3{0.0, 0.0, -1.0}};
    const Inference result = plnr::geo::infer(model, ray, ctx);

    EXPECT_EQ(result.kind, InferenceKind::OnAxis);
    EXPECT_TRUE(almostEqual(result.pos, Vec3{3.0, 0.0, 0.0}, 1e-9));
}

TEST(InferTest, GroundPlaneIntersectionPosition) {
    Model model;
    InferenceContext ctx;
    ctx.tols = PickOptions{0.05, 0.05};

    const Ray ray{Vec3{0.0, 0.0, 5.0}, normalized(Vec3{1.0, 0.0, -1.0})};
    const Inference result = plnr::geo::infer(model, ray, ctx);

    EXPECT_EQ(result.kind, InferenceKind::GroundPlane);
    EXPECT_TRUE(almostEqual(result.pos, Vec3{5.0, 0.0, 0.0}, 1e-9));
}

TEST(InferTest, RayParallelToGroundWithNoCandidatesIsNone) {
    Model model;
    InferenceContext ctx;
    ctx.tols = PickOptions{0.05, 0.05};

    const Ray ray{Vec3{0.0, 0.0, 5.0}, Vec3{1.0, 0.0, 0.0}};
    const Inference result = plnr::geo::infer(model, ray, ctx);

    EXPECT_EQ(result.kind, InferenceKind::None);
}

// New InferenceContext fields must be appended after tols so this
// positional 3-arg aggregate init keeps compiling.
TEST(InferTest, PositionalThreeArgConstructionStillCompiles) {
    Model model;
    ASSERT_TRUE(model.addEdge(Vec3{0.0, 0.0, 0.0}, Vec3{1.0, 0.0, 0.0}).created);
    const Vertex* v0 = model.findVertex(Vec3{0.0, 0.0, 0.0});
    ASSERT_NE(v0, nullptr);

    const InferenceContext ctx{std::nullopt, std::nullopt, PickOptions{0.1, 0.1}};
    const Ray ray{Vec3{0.0, 0.0, 5.0}, Vec3{0.0, 0.0, -1.0}};
    const Inference result = plnr::geo::infer(model, ray, ctx);

    EXPECT_EQ(result.kind, InferenceKind::Endpoint);
    EXPECT_EQ(result.refId, v0->id);
}

TEST(InferTest, MidpointBeatsOnEdge) {
    Model model;
    ASSERT_TRUE(model.addEdge(Vec3{0.0, 0.0, 0.0}, Vec3{2.0, 0.0, 0.0}).created);
    const auto* edge = model.edges().empty() ? nullptr : &model.edges().begin()->second;
    ASSERT_NE(edge, nullptr);

    InferenceContext ctx;
    // vertexTol and edgeTol are generous enough that OnEdge would also match
    // this point -- only priority order decides Midpoint wins.
    ctx.tols = PickOptions{0.1, 0.1};

    const Ray ray{Vec3{1.0, 0.0, 5.0}, Vec3{0.0, 0.0, -1.0}};
    const Inference result = plnr::geo::infer(model, ray, ctx);

    EXPECT_EQ(result.kind, InferenceKind::Midpoint);
    EXPECT_EQ(result.refId, edge->id);
    EXPECT_TRUE(almostEqual(result.pos, Vec3{1.0, 0.0, 0.0}, 1e-9));
}

TEST(InferTest, EndpointBeatsMidpointAtSharedLocation) {
    Model model;
    // edge1's midpoint sits at (1, 0, 0) -- exactly where edge2 has a vertex.
    ASSERT_TRUE(model.addEdge(Vec3{0.0, 0.0, 0.0}, Vec3{2.0, 0.0, 0.0}).created);
    ASSERT_TRUE(model.addEdge(Vec3{1.0, 0.0, 0.0}, Vec3{1.0, 5.0, 0.0}).created);
    const Vertex* shared = model.findVertex(Vec3{1.0, 0.0, 0.0});
    ASSERT_NE(shared, nullptr);

    InferenceContext ctx;
    ctx.tols = PickOptions{0.1, 0.1};

    const Ray ray{Vec3{1.0, 0.0, 5.0}, Vec3{0.0, 0.0, -1.0}};
    const Inference result = plnr::geo::infer(model, ray, ctx);

    EXPECT_EQ(result.kind, InferenceKind::Endpoint);
    EXPECT_EQ(result.refId, shared->id);
}

TEST(InferTest, IntersectionOfCrossingCoplanarEdges) {
    Model model;
    // Both edges lie in z = 0, crossing at (1, 0, 0) -- neither edge's
    // midpoint nor endpoint, so only the Intersection step can produce it.
    ASSERT_TRUE(model.addEdge(Vec3{0.0, 0.0, 0.0}, Vec3{4.0, 0.0, 0.0}).created);
    ASSERT_TRUE(model.addEdge(Vec3{1.0, -3.0, 0.0}, Vec3{1.0, 1.0, 0.0}).created);

    InferenceContext ctx;
    ctx.tols = PickOptions{0.05, 0.05};

    const Ray ray{Vec3{1.0, 0.0, 5.0}, Vec3{0.0, 0.0, -1.0}};
    const Inference result = plnr::geo::infer(model, ray, ctx);

    EXPECT_EQ(result.kind, InferenceKind::Intersection);
    EXPECT_TRUE(almostEqual(result.pos, Vec3{1.0, 0.0, 0.0}, 1e-6));
}

TEST(InferTest, GuidePointSnaps) {
    Model model;  // empty -- nothing else can compete
    InferenceContext ctx;
    ctx.tols = PickOptions{0.05, 0.05};
    ctx.guidePoints = {GuidePointData{Vec3{5.0, 5.0, 0.0}, Id{101}}};

    const Ray ray{Vec3{5.0, 5.0, 5.0}, Vec3{0.0, 0.0, -1.0}};
    const Inference result = plnr::geo::infer(model, ray, ctx);

    EXPECT_EQ(result.kind, InferenceKind::GuidePoint);
    EXPECT_EQ(result.refId, Id{101});
    EXPECT_TRUE(almostEqual(result.pos, Vec3{5.0, 5.0, 0.0}, 1e-9));
}

TEST(InferTest, GuideLineSnaps) {
    Model model;  // empty -- nothing else can compete
    InferenceContext ctx;
    ctx.tols = PickOptions{0.05, 0.1};
    ctx.guideLines = {GuideLineData{Vec3{0.0, 10.0, 0.0}, Vec3{1.0, 0.0, 0.0}, Id{202}}};

    const Ray ray{Vec3{5.0, 10.0, 5.0}, Vec3{0.0, 0.0, -1.0}};
    const Inference result = plnr::geo::infer(model, ray, ctx);

    EXPECT_EQ(result.kind, InferenceKind::GuideLine);
    EXPECT_EQ(result.refId, Id{202});
    ASSERT_TRUE(result.dir.has_value());
    EXPECT_TRUE(almostEqual(*result.dir, Vec3{1.0, 0.0, 0.0}, 1e-9));
    EXPECT_TRUE(almostEqual(result.pos, Vec3{5.0, 10.0, 0.0}, 1e-9));
}

TEST(InferTest, OnFaceHitsInterior) {
    Model model;
    // A 4x4 horizontal square loop at z = 5; closing it auto-detects a face.
    ASSERT_TRUE(model.addEdge(Vec3{0.0, 0.0, 5.0}, Vec3{4.0, 0.0, 5.0}).created);
    ASSERT_TRUE(model.addEdge(Vec3{4.0, 0.0, 5.0}, Vec3{4.0, 4.0, 5.0}).created);
    ASSERT_TRUE(model.addEdge(Vec3{4.0, 4.0, 5.0}, Vec3{0.0, 4.0, 5.0}).created);
    const auto closing = model.addEdge(Vec3{0.0, 4.0, 5.0}, Vec3{0.0, 0.0, 5.0});
    ASSERT_TRUE(closing.created);
    ASSERT_EQ(model.faces().size(), 1u);
    const Id faceId = model.faces().begin()->first;

    InferenceContext ctx;
    ctx.tols = PickOptions{0.05, 0.05};

    // (2, 2, 5) is the face's interior centroid, nowhere near any vertex or edge.
    const Ray ray{Vec3{2.0, 2.0, 10.0}, Vec3{0.0, 0.0, -1.0}};
    const Inference result = plnr::geo::infer(model, ray, ctx);

    EXPECT_EQ(result.kind, InferenceKind::OnFace);
    EXPECT_EQ(result.refId, faceId);
    EXPECT_TRUE(almostEqual(result.pos, Vec3{2.0, 2.0, 5.0}, 1e-6));
}

TEST(InferTest, FromPointAxisAlignment) {
    Model model;  // empty -- nothing else can compete
    InferenceContext ctx;
    ctx.tols = PickOptions{0.05, 0.1};
    ctx.chargedAnchors = {Vec3{2.0, 2.0, 0.0}};

    // Straight down through (2, 7, *): 5 units off every axis line through
    // the anchor except the Y-axis line (points (2, y, 0)), which the ray
    // crosses exactly at (2, 7, 0).
    const Ray ray{Vec3{2.0, 7.0, 5.0}, Vec3{0.0, 0.0, -1.0}};
    const Inference result = plnr::geo::infer(model, ray, ctx);

    EXPECT_EQ(result.kind, InferenceKind::FromPoint);
    ASSERT_TRUE(result.dir.has_value());
    EXPECT_TRUE(almostEqual(*result.dir, Vec3{0.0, 1.0, 0.0}, 1e-9));
    ASSERT_TRUE(result.source.has_value());
    EXPECT_TRUE(almostEqual(*result.source, Vec3{2.0, 2.0, 0.0}, 1e-9));
    EXPECT_TRUE(almostEqual(result.pos, Vec3{2.0, 7.0, 0.0}, 1e-9));
}

TEST(InferTest, ParallelSnapsToReferenceEdgeDirection) {
    Model model;  // empty -- nothing else can compete
    InferenceContext ctx;
    ctx.tols = PickOptions{0.05, 0.1};
    ctx.anchor = Vec3{5.0, 5.0, 0.0};
    ctx.referenceEdge = std::make_pair(Vec3{0.0, 0.0, 0.0}, Vec3{1.0, 0.0, 0.0});

    // (8, 5, 0) sits on the parallel candidate (through the anchor, along X)
    // but 3 units off the perpendicular candidate -- only Parallel can snap.
    const Ray ray{Vec3{8.0, 5.0, 5.0}, Vec3{0.0, 0.0, -1.0}};
    const Inference result = plnr::geo::infer(model, ray, ctx);

    EXPECT_EQ(result.kind, InferenceKind::Parallel);
    ASSERT_TRUE(result.dir.has_value());
    EXPECT_TRUE(almostEqual(*result.dir, Vec3{1.0, 0.0, 0.0}, 1e-9));
    EXPECT_TRUE(almostEqual(result.pos, Vec3{8.0, 5.0, 0.0}, 1e-9));
}

TEST(InferTest, PerpendicularSnapsInGroundPlane) {
    Model model;  // empty -- nothing else can compete
    InferenceContext ctx;
    ctx.tols = PickOptions{0.05, 0.1};
    ctx.anchor = Vec3{5.0, 5.0, 0.0};
    ctx.referenceEdge = std::make_pair(Vec3{0.0, 0.0, 0.0}, Vec3{1.0, 0.0, 0.0});

    // (5, 9, 0) sits on the perpendicular candidate (through the anchor,
    // along Y) but 4 units off the parallel one -- only Perpendicular can snap.
    const Ray ray{Vec3{5.0, 9.0, 5.0}, Vec3{0.0, 0.0, -1.0}};
    const Inference result = plnr::geo::infer(model, ray, ctx);

    EXPECT_EQ(result.kind, InferenceKind::Perpendicular);
    ASSERT_TRUE(result.dir.has_value());
    // cross(refDir, world +Z) = (0, -1, 0): perpendicular to refDir, unit,
    // and lying in the ground plane (z component 0).
    EXPECT_TRUE(almostEqual(*result.dir, Vec3{0.0, -1.0, 0.0}, 1e-9));
    EXPECT_TRUE(almostEqual(result.pos, Vec3{5.0, 9.0, 0.0}, 1e-9));
}

}  // namespace
