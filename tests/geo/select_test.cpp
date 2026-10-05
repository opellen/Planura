#include <geo/select.h>

#include <algorithm>
#include <array>

#include <gtest/gtest.h>

namespace {

using plnr::geo::collectVertices;
using plnr::geo::ConnectedSet;
using plnr::geo::EntityKind;
using plnr::geo::faceBoundaryEdges;
using plnr::geo::Frustum;
using plnr::geo::frustumFromCornerRays;
using plnr::geo::Id;
using plnr::geo::Model;
using plnr::geo::pickInFrustum;
using plnr::geo::Ray;
using plnr::geo::RegionMode;
using plnr::geo::RegionPick;
using plnr::geo::Vec3;

// True if edges a and b (as returned by faceBoundaryEdges) share exactly one
// endpoint vertex -- confirms consecutive entries are loop-adjacent, not
// just an unordered set of the right ids.
bool shareOneVertex(const Model& model, Id a, Id b) {
    const std::vector<Id> va = collectVertices(model, EntityKind::Edge, a);
    const std::vector<Id> vb = collectVertices(model, EntityKind::Edge, b);
    int shared = 0;
    for (Id x : va) {
        if (std::find(vb.begin(), vb.end(), x) != vb.end()) {
            ++shared;
        }
    }
    return shared == 1;
}

// Builds a closed rectangle (4 edges + 1 face) and returns its edge ids in
// addEdge call order (p0p1, p1p2, p2p3, p3p0).
struct Rectangle {
    std::vector<Id> edges;
    Id face{};
};

Rectangle makeRectangle(Model& model, Vec3 origin) {
    const Vec3 p0 = origin;
    const Vec3 p1 = origin + Vec3{4.0, 0.0, 0.0};
    const Vec3 p2 = origin + Vec3{4.0, 3.0, 0.0};
    const Vec3 p3 = origin + Vec3{0.0, 3.0, 0.0};

    Rectangle rect;
    rect.edges.push_back(model.addEdge(p0, p1).edge);
    rect.edges.push_back(model.addEdge(p1, p2).edge);
    rect.edges.push_back(model.addEdge(p2, p3).edge);
    const auto closing = model.addEdge(p3, p0);
    rect.edges.push_back(closing.edge);
    rect.face = closing.newFaces.empty() ? plnr::geo::kInvalidId : closing.newFaces[0];
    return rect;
}

TEST(SelectTest, FaceBoundaryEdgesOfRectangleReturnsFourEdgesInLoopOrder) {
    Model model;
    const Rectangle rect = makeRectangle(model, Vec3{0.0, 0.0, 0.0});
    ASSERT_NE(rect.face, plnr::geo::kInvalidId);

    const std::vector<Id> boundary = faceBoundaryEdges(model, rect.face);

    ASSERT_EQ(boundary.size(), 4u);
    // Same 4 edges as were added, as a set.
    std::vector<Id> sortedBoundary = boundary;
    std::vector<Id> sortedExpected = rect.edges;
    std::sort(sortedBoundary.begin(), sortedBoundary.end());
    std::sort(sortedExpected.begin(), sortedExpected.end());
    EXPECT_EQ(sortedBoundary, sortedExpected);

    // Loop order: each consecutive pair (wrapping) shares exactly one vertex.
    for (std::size_t i = 0; i < boundary.size(); ++i) {
        const Id a = boundary[i];
        const Id b = boundary[(i + 1) % boundary.size()];
        EXPECT_TRUE(shareOneVertex(model, a, b)) << "edges " << a << " and " << b << " not loop-adjacent";
    }
}

TEST(SelectTest, FaceBoundaryEdgesOfUnknownIdIsEmpty) {
    Model model;
    const Rectangle rect = makeRectangle(model, Vec3{0.0, 0.0, 0.0});
    (void)rect;

    EXPECT_TRUE(faceBoundaryEdges(model, 999999).empty());
}

TEST(SelectTest, ConnectedComponentFromVertexSeedOfFacedRectangleReturnsWholeLoop) {
    Model model;
    const Rectangle rect = makeRectangle(model, Vec3{0.0, 0.0, 0.0});
    ASSERT_NE(rect.face, plnr::geo::kInvalidId);
    const std::vector<Id> vertices = collectVertices(model, EntityKind::Face, rect.face);
    ASSERT_EQ(vertices.size(), 4u);

    const ConnectedSet set = plnr::geo::connectedComponent(model, EntityKind::Vertex, vertices[0]);

    EXPECT_EQ(set.vertices.size(), 4u);
    EXPECT_EQ(set.edges.size(), 4u);
    ASSERT_EQ(set.faces.size(), 1u);
    EXPECT_EQ(set.faces[0], rect.face);
}

TEST(SelectTest, ConnectedComponentFromEdgeSeedOfFacedRectangleReturnsWholeLoop) {
    Model model;
    const Rectangle rect = makeRectangle(model, Vec3{0.0, 0.0, 0.0});
    ASSERT_NE(rect.face, plnr::geo::kInvalidId);

    const ConnectedSet set = plnr::geo::connectedComponent(model, EntityKind::Edge, rect.edges[0]);

    EXPECT_EQ(set.vertices.size(), 4u);
    EXPECT_EQ(set.edges.size(), 4u);
    ASSERT_EQ(set.faces.size(), 1u);
    EXPECT_EQ(set.faces[0], rect.face);
}

TEST(SelectTest, ConnectedComponentFromFaceSeedOfFacedRectangleReturnsWholeLoop) {
    Model model;
    const Rectangle rect = makeRectangle(model, Vec3{0.0, 0.0, 0.0});
    ASSERT_NE(rect.face, plnr::geo::kInvalidId);

    const ConnectedSet set = plnr::geo::connectedComponent(model, EntityKind::Face, rect.face);

    EXPECT_EQ(set.vertices.size(), 4u);
    EXPECT_EQ(set.edges.size(), 4u);
    ASSERT_EQ(set.faces.size(), 1u);
    EXPECT_EQ(set.faces[0], rect.face);
}

TEST(SelectTest, ConnectedComponentOfDisjointRectanglesOnlyIncludesSeededOne) {
    Model model;
    const Rectangle rectA = makeRectangle(model, Vec3{0.0, 0.0, 0.0});
    const Rectangle rectB = makeRectangle(model, Vec3{100.0, 0.0, 0.0});
    ASSERT_NE(rectA.face, plnr::geo::kInvalidId);
    ASSERT_NE(rectB.face, plnr::geo::kInvalidId);

    const ConnectedSet set = plnr::geo::connectedComponent(model, EntityKind::Face, rectA.face);

    ASSERT_EQ(set.faces.size(), 1u);
    EXPECT_EQ(set.faces[0], rectA.face);
    ASSERT_EQ(set.edges.size(), 4u);
    for (Id e : set.edges) {
        EXPECT_TRUE(std::find(rectA.edges.begin(), rectA.edges.end(), e) != rectA.edges.end());
        EXPECT_TRUE(std::find(rectB.edges.begin(), rectB.edges.end(), e) == rectB.edges.end());
    }
}

TEST(SelectTest, ConnectedComponentOfWireEdgeChainTraversesSharedVertices) {
    Model model;
    // p0-p1-p2-p3 open chain -- no closing edge, so no face forms.
    const Vec3 p0{0.0, 0.0, 0.0};
    const Vec3 p1{1.0, 0.0, 0.0};
    const Vec3 p2{2.0, 0.0, 0.0};
    const Vec3 p3{3.0, 0.0, 0.0};
    const Id e0 = model.addEdge(p0, p1).edge;
    ASSERT_NE(e0, plnr::geo::kInvalidId);
    ASSERT_TRUE(model.addEdge(p1, p2).created);
    ASSERT_TRUE(model.addEdge(p2, p3).created);
    ASSERT_TRUE(model.faces().empty());

    const ConnectedSet set = plnr::geo::connectedComponent(model, EntityKind::Edge, e0);

    EXPECT_EQ(set.vertices.size(), 4u);
    EXPECT_EQ(set.edges.size(), 3u);
    EXPECT_TRUE(set.faces.empty());
}

TEST(SelectTest, ConnectedComponentOfUnknownSeedIsEmpty) {
    Model model;
    const Rectangle rect = makeRectangle(model, Vec3{0.0, 0.0, 0.0});
    (void)rect;

    const ConnectedSet set = plnr::geo::connectedComponent(model, EntityKind::Vertex, 999999);

    EXPECT_TRUE(set.vertices.empty());
    EXPECT_TRUE(set.edges.empty());
    EXPECT_TRUE(set.faces.empty());
}

// Builds 4 converging corner rays for a screen-axis-aligned drag rectangle,
// as if a camera sat above the rectangle's center looking down. Rays must
// converge (not run parallel), or the frustum's side planes become degenerate.
std::array<Ray, 4> cornerRaysAbove(double minX, double maxX, double minY, double maxY) {
    const Vec3 eye{(minX + maxX) / 2.0, (minY + maxY) / 2.0, 10.0};
    const Vec3 tl{minX, maxY, 0.0};
    const Vec3 tr{maxX, maxY, 0.0};
    const Vec3 br{maxX, minY, 0.0};
    const Vec3 bl{minX, minY, 0.0};
    return {
        Ray{eye, plnr::geo::normalized(tl - eye)},
        Ray{eye, plnr::geo::normalized(tr - eye)},
        Ray{eye, plnr::geo::normalized(br - eye)},
        Ray{eye, plnr::geo::normalized(bl - eye)},
    };
}

TEST(SelectTest, PickInFrustumWindowFullyContainedSelectsEverything) {
    Model model;
    const Rectangle rect = makeRectangle(model, Vec3{0.0, 0.0, 0.0});  // 0<=x<=4, 0<=y<=3
    ASSERT_NE(rect.face, plnr::geo::kInvalidId);

    const Frustum f = frustumFromCornerRays(cornerRaysAbove(-1.0, 5.0, -1.0, 4.0));
    const RegionPick pick = pickInFrustum(model, f, RegionMode::Window);

    EXPECT_EQ(pick.vertices.size(), 4u);
    EXPECT_EQ(pick.edges.size(), 4u);
    ASSERT_EQ(pick.faces.size(), 1u);
    EXPECT_EQ(pick.faces[0], rect.face);
}

TEST(SelectTest, PickInFrustumWindowPartiallyOverlappingSelectsOnlyFullyContained) {
    Model model;
    const Rectangle rect = makeRectangle(model, Vec3{0.0, 0.0, 0.0});  // p0(0,0) p1(4,0) p2(4,3) p3(0,3)

    // Covers only the left half (x in [-1, 2]) -- p0 and p3 (x=0) are inside,
    // p1 and p2 (x=4) are not, so only edge p3-p0 (rect.edges[3]) has both
    // endpoints inside.
    const Frustum f = frustumFromCornerRays(cornerRaysAbove(-1.0, 2.0, -1.0, 4.0));
    const RegionPick pick = pickInFrustum(model, f, RegionMode::Window);

    EXPECT_EQ(pick.vertices.size(), 2u);
    ASSERT_EQ(pick.edges.size(), 1u);
    EXPECT_EQ(pick.edges[0], rect.edges[3]);
    EXPECT_TRUE(pick.faces.empty());  // not every loop vertex is inside
}

TEST(SelectTest, PickInFrustumCrossingOverEdgeMidsectionWithBothEndpointsOutsideSelectsIt) {
    Model model;
    // A single wire edge, no face -- isolates the edge-crossing rule (segment
    // clip) from the face-via-boundary-edge rule tested separately below.
    const Vec3 p0{0.0, 0.0, 0.0};
    const Vec3 p1{4.0, 0.0, 0.0};
    const Id edgeId = model.addEdge(p0, p1).edge;
    ASSERT_NE(edgeId, plnr::geo::kInvalidId);

    // Thin box around the midpoint (2, 0) -- neither endpoint (x=0 or x=4)
    // is inside this x range.
    const Frustum f = frustumFromCornerRays(cornerRaysAbove(1.5, 2.5, -1.0, 1.0));
    const RegionPick pick = pickInFrustum(model, f, RegionMode::Crossing);

    ASSERT_EQ(pick.edges.size(), 1u);
    EXPECT_EQ(pick.edges[0], edgeId);
    EXPECT_TRUE(pick.vertices.empty());
}

TEST(SelectTest, PickInFrustumCrossingPartiallyOverlappingFacedRectangleSelectsFaceViaCrossedBoundaryEdge) {
    Model model;
    const Rectangle rect = makeRectangle(model, Vec3{0.0, 0.0, 0.0});  // bottom edge p0(0,0)-p1(4,0)
    ASSERT_NE(rect.face, plnr::geo::kInvalidId);

    // Same thin box as above, over the rectangle's bottom edge's midsection
    // only -- doesn't reach either endpoint, and stays far from the other 3
    // edges (all at y=3 or requiring x=0/x=4 exactly).
    const Frustum f = frustumFromCornerRays(cornerRaysAbove(1.5, 2.5, -1.0, 1.0));
    const RegionPick pick = pickInFrustum(model, f, RegionMode::Crossing);

    ASSERT_EQ(pick.edges.size(), 1u);
    EXPECT_EQ(pick.edges[0], rect.edges[0]);
    ASSERT_EQ(pick.faces.size(), 1u);
    EXPECT_EQ(pick.faces[0], rect.face);
    EXPECT_TRUE(pick.vertices.empty());
}

TEST(SelectTest, PickInFrustumFilterExcludesVerticesButNotEdgesOrFaces) {
    Model model;
    const Rectangle rect = makeRectangle(model, Vec3{0.0, 0.0, 0.0});
    ASSERT_NE(rect.face, plnr::geo::kInvalidId);

    const Frustum f = frustumFromCornerRays(cornerRaysAbove(-1.0, 5.0, -1.0, 4.0));  // fully contains it
    const auto noVertices = [](EntityKind kind, Id) { return kind != EntityKind::Vertex; };
    const RegionPick pick = pickInFrustum(model, f, RegionMode::Window, noVertices);

    EXPECT_TRUE(pick.vertices.empty());
    EXPECT_EQ(pick.edges.size(), 4u);
    ASSERT_EQ(pick.faces.size(), 1u);
    EXPECT_EQ(pick.faces[0], rect.face);
}

}  // namespace
