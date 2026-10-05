#include <geo/model.h>

#include <gtest/gtest.h>

namespace {

using plnr::geo::kInvalidId;
using plnr::geo::kMergeTol;
using plnr::geo::Model;
using plnr::geo::Vec3;

TEST(ModelTest, SharedEndpointWithinTolMergesToThreeVertices) {
    Model model;

    const auto e1 = model.addEdge(Vec3{0.0, 0.0, 0.0}, Vec3{1.0, 0.0, 0.0});
    ASSERT_TRUE(e1.created);

    // Second edge starts at a point just within kMergeTol of (1,0,0): must
    // merge onto the existing vertex instead of creating a fourth one.
    const Vec3 nearShared{1.0 + kMergeTol * 0.5, 0.0, 0.0};
    const auto e2 = model.addEdge(nearShared, Vec3{2.0, 0.0, 0.0});
    ASSERT_TRUE(e2.created);

    EXPECT_EQ(model.vertices().size(), 3u);
    EXPECT_EQ(model.edges().size(), 2u);
}

TEST(ModelTest, RejectedZeroLengthEdgeLeavesModelUntouched) {
    Model model;

    // Brand-new coincident endpoints: rejected AND no stray vertex remains.
    const auto rejected = model.addEdge(Vec3{5.0, 5.0, 0.0}, Vec3{5.0, 5.0, 0.0});
    EXPECT_FALSE(rejected.created);
    EXPECT_EQ(rejected.edge, kInvalidId);
    EXPECT_TRUE(model.vertices().empty());
    EXPECT_TRUE(model.edges().empty());

    // A reject touching an EXISTING vertex must not erase it.
    ASSERT_TRUE(model.addEdge(Vec3{0.0, 0.0, 0.0}, Vec3{1.0, 0.0, 0.0}).created);
    const auto rejectedOnExisting = model.addEdge(Vec3{1.0, 0.0, 0.0}, Vec3{1.0, 0.0, 0.0});
    EXPECT_FALSE(rejectedOnExisting.created);
    EXPECT_EQ(model.vertices().size(), 2u);
}

TEST(ModelTest, DuplicateEdgeEitherOrderReturnsSameIdNotCreated) {
    Model model;

    const auto first = model.addEdge(Vec3{0.0, 0.0, 0.0}, Vec3{1.0, 0.0, 0.0});
    ASSERT_TRUE(first.created);

    const auto sameOrder = model.addEdge(Vec3{0.0, 0.0, 0.0}, Vec3{1.0, 0.0, 0.0});
    EXPECT_FALSE(sameOrder.created);
    EXPECT_EQ(sameOrder.edge, first.edge);

    const auto reversedOrder = model.addEdge(Vec3{1.0, 0.0, 0.0}, Vec3{0.0, 0.0, 0.0});
    EXPECT_FALSE(reversedOrder.created);
    EXPECT_EQ(reversedOrder.edge, first.edge);

    EXPECT_EQ(model.edges().size(), 1u);
}

TEST(ModelTest, ZeroLengthEdgeIsRejected) {
    Model model;

    // Same exact point: rejected, and the model stays untouched -- a rejected
    // call must not leave a stray vertex behind.
    const auto sameExactPoint = model.addEdge(Vec3{0.0, 0.0, 0.0}, Vec3{0.0, 0.0, 0.0});
    EXPECT_FALSE(sameExactPoint.created);
    EXPECT_EQ(sameExactPoint.edge, kInvalidId);
    EXPECT_TRUE(model.edges().empty());
    EXPECT_TRUE(model.vertices().empty());

    const Vec3 within{kMergeTol * 0.5, 0.0, 0.0};
    const auto withinTol = model.addEdge(Vec3{0.0, 0.0, 0.0}, within);
    EXPECT_FALSE(withinTol.created);
    EXPECT_EQ(withinTol.edge, kInvalidId);
    EXPECT_TRUE(model.edges().empty());
}

TEST(ModelTest, RemoveEdgeGarbageCollectsOrphanButKeepsSharedVertex) {
    Model model;

    const auto e1 = model.addEdge(Vec3{0.0, 0.0, 0.0}, Vec3{1.0, 0.0, 0.0});
    const auto e2 = model.addEdge(Vec3{1.0, 0.0, 0.0}, Vec3{2.0, 0.0, 0.0});
    ASSERT_TRUE(e1.created);
    ASSERT_TRUE(e2.created);
    ASSERT_EQ(model.vertices().size(), 3u);

    // Remove e1: its unique endpoint (0,0,0) becomes orphaned and must be
    // garbage-collected, but the shared vertex (1,0,0) is still used by e2
    // and must survive.
    ASSERT_TRUE(model.removeEdge(e1.edge));

    EXPECT_EQ(model.edges().size(), 1u);
    EXPECT_EQ(model.vertices().size(), 2u);
    EXPECT_EQ(model.findVertex(Vec3{0.0, 0.0, 0.0}), nullptr);
    EXPECT_NE(model.findVertex(Vec3{1.0, 0.0, 0.0}), nullptr);
    EXPECT_NE(model.findVertex(Vec3{2.0, 0.0, 0.0}), nullptr);
}

TEST(ModelTest, IdsAreNeverReusedAfterRemoval) {
    Model model;

    const auto e1 = model.addEdge(Vec3{0.0, 0.0, 0.0}, Vec3{1.0, 0.0, 0.0});
    ASSERT_TRUE(e1.created);

    ASSERT_TRUE(model.removeEdge(e1.edge));

    const auto e2 = model.addEdge(Vec3{10.0, 0.0, 0.0}, Vec3{11.0, 0.0, 0.0});
    ASSERT_TRUE(e2.created);

    EXPECT_NE(e2.edge, e1.edge);
    EXPECT_GT(e2.edge, e1.edge);
}

TEST(ModelTest, FindVertexRespectsTolerance) {
    Model model;
    model.addEdge(Vec3{0.0, 0.0, 0.0}, Vec3{5.0, 0.0, 0.0});

    const Vec3 justInside{kMergeTol * 0.9, 0.0, 0.0};
    const Vec3 justOutside{kMergeTol * 2.0, 0.0, 0.0};

    EXPECT_NE(model.findVertex(justInside), nullptr);
    EXPECT_EQ(model.findVertex(justOutside), nullptr);
    EXPECT_NE(model.findVertex(justOutside, kMergeTol * 3.0), nullptr);
}

TEST(ModelTest, UnknownIdAccessorsReturnNullptr) {
    Model model;
    model.addEdge(Vec3{0.0, 0.0, 0.0}, Vec3{1.0, 0.0, 0.0});

    constexpr plnr::geo::Id unknown = 999999;

    EXPECT_EQ(model.vertex(unknown), nullptr);
    EXPECT_EQ(model.edge(unknown), nullptr);
    EXPECT_EQ(model.halfEdge(unknown), nullptr);
    EXPECT_EQ(model.face(unknown), nullptr);
}

TEST(ModelTest, RemoveEdgeUnknownIdReturnsFalse) {
    Model model;

    EXPECT_FALSE(model.removeEdge(12345));
}

TEST(ModelTest, DetectFacesFalseSkipsAutoFaceCreationOnly) {
    // The same closing triangle edge that auto-creates a face by default
    // creates none when the caller opts out (sweep contract -- see addEdge's
    // header comment); edges/vertices are unaffected either way.
    const Vec3 a{0.0, 0.0, 0.0};
    const Vec3 b{1.0, 0.0, 0.0};
    const Vec3 c{0.0, 1.0, 0.0};

    Model detecting;
    detecting.addEdge(a, b);
    detecting.addEdge(b, c);
    const plnr::geo::AddEdgeResult closedDefault = detecting.addEdge(c, a);
    EXPECT_EQ(closedDefault.newFaces.size(), 1u);
    EXPECT_EQ(detecting.faces().size(), 1u);

    Model raw;
    raw.addEdge(a, b, /*detectFaces=*/false);
    raw.addEdge(b, c, /*detectFaces=*/false);
    const plnr::geo::AddEdgeResult closedRaw = raw.addEdge(c, a, /*detectFaces=*/false);
    EXPECT_TRUE(closedRaw.created);
    EXPECT_TRUE(closedRaw.newFaces.empty());
    EXPECT_EQ(raw.faces().size(), 0u);
    EXPECT_EQ(raw.edges().size(), 3u);
    EXPECT_EQ(raw.vertices().size(), 3u);
}

}  // namespace
