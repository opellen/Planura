#include <geo/triangulate.h>

#include <vector>

#include <gtest/gtest.h>

namespace {

using plnr::geo::cross;
using plnr::geo::dot;
using plnr::geo::Face;
using plnr::geo::Id;
using plnr::geo::Model;
using plnr::geo::Vec3;
using plnr::geo::Vertex;

// Asserts every triangle in tris (ids, 3 per triangle) is wound the same
// way as face->normal: cross(b - a, c - a) . normal > 0.
void expectConsistentWinding(const Model& model, const Face& face, const std::vector<Id>& tris) {
    for (std::size_t i = 0; i < tris.size(); i += 3) {
        const Vertex* va = model.vertex(tris[i]);
        const Vertex* vb = model.vertex(tris[i + 1]);
        const Vertex* vc = model.vertex(tris[i + 2]);
        ASSERT_NE(va, nullptr);
        ASSERT_NE(vb, nullptr);
        ASSERT_NE(vc, nullptr);

        const Vec3 n = cross(vb->pos - va->pos, vc->pos - va->pos);
        EXPECT_GT(dot(n, face.normal), 0.0);
    }
}

TEST(TriangulateTest, SquareProducesTwoTrianglesFromFaceVertices) {
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

    const Id faceId = closing.newFaces[0];
    const Face* face = model.face(faceId);
    ASSERT_NE(face, nullptr);

    const std::vector<Id> tris = plnr::geo::triangulate(model, faceId);
    ASSERT_EQ(tris.size(), 6u);  // 2 triangles

    for (Id id : tris) {
        EXPECT_NE(model.vertex(id), nullptr);
    }

    expectConsistentWinding(model, *face, tris);
}

TEST(TriangulateTest, ConcaveLShapeProducesFourTriangles) {
    Model model;
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

    const Id faceId = closing.newFaces[0];
    const Face* face = model.face(faceId);
    ASSERT_NE(face, nullptr);

    const std::vector<Id> tris = plnr::geo::triangulate(model, faceId);
    ASSERT_EQ(tris.size(), 12u);  // 4 triangles

    for (Id id : tris) {
        EXPECT_NE(model.vertex(id), nullptr);
    }

    expectConsistentWinding(model, *face, tris);
}

TEST(TriangulateTest, UnknownFaceIdReturnsEmpty) {
    Model model;
    ASSERT_TRUE(model.addEdge(Vec3{0.0, 0.0, 0.0}, Vec3{1.0, 0.0, 0.0}).created);

    const std::vector<Id> tris = plnr::geo::triangulate(model, 999999);
    EXPECT_TRUE(tris.empty());
}

}  // namespace
