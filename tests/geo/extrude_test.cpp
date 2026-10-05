#include <geo/model.h>

#include <cmath>
#include <tuple>
#include <vector>

#include <gtest/gtest.h>

namespace {

using plnr::geo::ExtrudeResult;
using plnr::geo::Id;
using plnr::geo::kInvalidId;
using plnr::geo::Model;
using plnr::geo::Vec3;

// 4-vertex, 4-edge, 1-face rectangle in the z=0 plane (same construction
// pattern as tests/geo/loop_test.cpp).
Id buildRectangleFace(Model& model, double width = 4.0, double depth = 3.0) {
    const Vec3 p0{0.0, 0.0, 0.0};
    const Vec3 p1{width, 0.0, 0.0};
    const Vec3 p2{width, depth, 0.0};
    const Vec3 p3{0.0, depth, 0.0};

    model.addEdge(p0, p1);
    model.addEdge(p1, p2);
    model.addEdge(p2, p3);
    const auto closing = model.addEdge(p3, p0);
    EXPECT_TRUE(closing.created);
    EXPECT_EQ(closing.newFaces.size(), 1u);
    return closing.newFaces.empty() ? kInvalidId : closing.newFaces[0];
}

Vec3 faceCentroid(const Model& model, Id faceId) {
    const std::vector<Id> loop = model.faceVertexLoop(faceId);
    Vec3 c{0.0, 0.0, 0.0};
    for (Id v : loop) {
        c = c + model.vertex(v)->pos;
    }
    return c * (1.0 / static_cast<double>(loop.size()));
}

Vec3 modelVertexCentroid(const Model& model) {
    Vec3 c{0.0, 0.0, 0.0};
    for (const auto& [id, v] : model.vertices()) {
        c = c + v.pos;
    }
    return c * (1.0 / static_cast<double>(model.vertices().size()));
}

// Fails (via EXPECT) unless every face's normal points away from the given
// centroid, i.e. dot(face.normal, faceCentroid - centroid) > 0.
void expectAllFacesOutward(const Model& model, const Vec3& centroid) {
    for (const auto& [id, face] : model.faces()) {
        const Vec3 fc = faceCentroid(model, id);
        EXPECT_GT(dot(face.normal, fc - centroid), 0.0) << "face " << id << " normal not outward";
    }
}

// Newell normal over faceId's committed loop order -- the GEOMETRIC winding,
// as opposed to the stored Face::normal.
Vec3 loopNewellNormal(const Model& model, Id faceId) {
    const std::vector<Id> loop = model.faceVertexLoop(faceId);
    Vec3 n{0.0, 0.0, 0.0};
    for (std::size_t i = 0; i < loop.size(); ++i) {
        const Vec3& a = model.vertex(loop[i])->pos;
        const Vec3& b = model.vertex(loop[(i + 1) % loop.size()])->pos;
        n.x += (a.y - b.y) * (a.z + b.z);
        n.y += (a.z - b.z) * (a.x + b.x);
        n.z += (a.x - b.x) * (a.y + b.y);
    }
    return n;
}

// Regression guard: a face's loop winding must agree with its stored
// normal, or the renderer draws it inside-out
void expectWindingMatchesNormals(const Model& model) {
    for (const auto& [id, face] : model.faces()) {
        EXPECT_GT(dot(loopNewellNormal(model, id), face.normal), 0.0)
            << "face " << id << " loop winding contradicts its stored normal";
    }
}

using SizeSnapshot = std::tuple<std::size_t, std::size_t, std::size_t, std::size_t>;

SizeSnapshot snapshot(const Model& model) {
    return {model.vertices().size(), model.edges().size(), model.halfEdges().size(), model.faces().size()};
}

TEST(ExtrudeTest, PositiveDistanceMakesOutwardBox) {
    Model model;
    const Id faceId = buildRectangleFace(model);
    ASSERT_NE(faceId, kInvalidId);

    const ExtrudeResult result = model.extrudeFace(faceId, 2.0);
    ASSERT_TRUE(result.ok);
    EXPECT_NE(result.capFace, kInvalidId);
    EXPECT_EQ(result.sideFaces.size(), 4u);
    EXPECT_EQ(result.newVertices.size(), 4u);

    EXPECT_EQ(model.vertices().size(), 8u);
    EXPECT_EQ(model.edges().size(), 12u);
    EXPECT_EQ(model.faces().size(), 6u);

    for (Id vId : result.newVertices) {
        EXPECT_NEAR(model.vertex(vId)->pos.z, 2.0, 1e-9);
    }

    expectAllFacesOutward(model, modelVertexCentroid(model));
    expectWindingMatchesNormals(model);
}

// Stacks a smaller box on the cap. Faces net +4 not +5 -- the cap's boundary
// edges are already claimed by box 1's sides, so "recreate reversed face"
// finds both windings taken and returns kInvalidId (Model::extrudeFace).
TEST(ExtrudeTest, ReExtrudingCapFaceStacksAnotherBox) {
    Model model;
    const Id faceId = buildRectangleFace(model);
    ASSERT_NE(faceId, kInvalidId);

    const ExtrudeResult first = model.extrudeFace(faceId, 2.0);
    ASSERT_TRUE(first.ok);
    ASSERT_EQ(model.faces().size(), 6u);
    ASSERT_EQ(model.edges().size(), 12u);
    ASSERT_EQ(model.vertices().size(), 8u);

    const ExtrudeResult second = model.extrudeFace(first.capFace, 1.0);
    ASSERT_TRUE(second.ok);
    EXPECT_EQ(second.sideFaces.size(), 4u);
    EXPECT_EQ(second.newVertices.size(), 4u);

    EXPECT_EQ(model.vertices().size(), 12u);
    EXPECT_EQ(model.edges().size(), 20u);
    EXPECT_EQ(model.faces().size(), 10u);

    for (Id vId : second.newVertices) {
        EXPECT_NEAR(model.vertex(vId)->pos.z, 3.0, 1e-9);
    }
}

TEST(ExtrudeTest, NegativeDistanceMakesOutwardBoxBelow) {
    Model model;
    const Id faceId = buildRectangleFace(model);
    ASSERT_NE(faceId, kInvalidId);

    const double d = 2.0;
    const ExtrudeResult result = model.extrudeFace(faceId, -d);
    ASSERT_TRUE(result.ok);

    EXPECT_EQ(model.vertices().size(), 8u);
    EXPECT_EQ(model.edges().size(), 12u);
    EXPECT_EQ(model.faces().size(), 6u);

    for (Id vId : result.newVertices) {
        EXPECT_NEAR(model.vertex(vId)->pos.z, -d, 1e-9);
    }

    expectAllFacesOutward(model, modelVertexCentroid(model));
    expectWindingMatchesNormals(model);
}

TEST(ExtrudeTest, TinyDistanceIsRejectedWithoutMutation) {
    Model model;
    const Id faceId = buildRectangleFace(model);
    ASSERT_NE(faceId, kInvalidId);

    const SizeSnapshot before = snapshot(model);
    const ExtrudeResult result = model.extrudeFace(faceId, 1e-6);  // < kMergeTol
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.capFace, kInvalidId);
    EXPECT_TRUE(result.sideFaces.empty());
    EXPECT_TRUE(result.newVertices.empty());
    EXPECT_EQ(snapshot(model), before);
}

TEST(ExtrudeTest, UnknownFaceIdIsRejectedWithoutMutation) {
    Model model;
    const Id faceId = buildRectangleFace(model);
    ASSERT_NE(faceId, kInvalidId);

    const SizeSnapshot before = snapshot(model);
    const ExtrudeResult result = model.extrudeFace(faceId + 1000, 2.0);
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(snapshot(model), before);
}

}  // namespace
