#include <geo/csg.h>

#include <cstddef>
#include <set>
#include <vector>

#include <gtest/gtest.h>

#include <geo/model.h>
#include <geo/scene.h>

namespace {

using plnr::geo::Id;
using plnr::geo::Model;
using plnr::geo::Transform;
using plnr::geo::Vec3;

namespace csg = plnr::geo::csg;

// Volume tolerance: axis-aligned box operands are analytically exact, but
// fan-triangulating dozens of unmerged fragments accumulates rounding, hence
// slack rather than 1e-12.
constexpr double kVolumeTol = 1e-6;

// Builds a box via 12 wire edges (detectFaces=false) plus 6 explicit faces.
// outward=false reverses every loop and negates every normal for the
// hollow-cube cavity pattern (see solid_test.cpp's makeBox).
void makeBox(Model& model, Vec3 minP, Vec3 maxP, bool outward = true) {
    const Vec3 v0p{minP.x, minP.y, minP.z};
    const Vec3 v1p{maxP.x, minP.y, minP.z};
    const Vec3 v2p{maxP.x, maxP.y, minP.z};
    const Vec3 v3p{minP.x, maxP.y, minP.z};
    const Vec3 v4p{minP.x, minP.y, maxP.z};
    const Vec3 v5p{maxP.x, minP.y, maxP.z};
    const Vec3 v6p{maxP.x, maxP.y, maxP.z};
    const Vec3 v7p{minP.x, maxP.y, maxP.z};

    model.addEdge(v0p, v3p, false);
    model.addEdge(v3p, v2p, false);
    model.addEdge(v2p, v1p, false);
    model.addEdge(v1p, v0p, false);
    model.addEdge(v4p, v5p, false);
    model.addEdge(v5p, v6p, false);
    model.addEdge(v6p, v7p, false);
    model.addEdge(v7p, v4p, false);
    model.addEdge(v0p, v4p, false);
    model.addEdge(v1p, v5p, false);
    model.addEdge(v2p, v6p, false);
    model.addEdge(v3p, v7p, false);

    const std::vector<Vec3> bottom = {v0p, v3p, v2p, v1p};
    const std::vector<Vec3> top = {v4p, v5p, v6p, v7p};
    const std::vector<Vec3> front = {v0p, v1p, v5p, v4p};
    const std::vector<Vec3> back = {v3p, v7p, v6p, v2p};
    const std::vector<Vec3> left = {v0p, v4p, v7p, v3p};
    const std::vector<Vec3> right = {v1p, v2p, v6p, v5p};

    const std::vector<Vec3>* loops[6] = {&bottom, &top, &front, &back, &left, &right};
    const Vec3 normals[6] = {
        Vec3{0.0, 0.0, -1.0}, Vec3{0.0, 0.0, 1.0}, Vec3{0.0, -1.0, 0.0},
        Vec3{0.0, 1.0, 0.0},  Vec3{-1.0, 0.0, 0.0}, Vec3{1.0, 0.0, 0.0},
    };

    for (int i = 0; i < 6; ++i) {
        std::vector<Id> loopIds;
        Vec3 normal = normals[i];
        if (outward) {
            for (const Vec3& p : *loops[i]) {
                loopIds.push_back(model.findVertex(p)->id);
            }
        } else {
            for (auto it = loops[i]->rbegin(); it != loops[i]->rend(); ++it) {
                loopIds.push_back(model.findVertex(*it)->id);
            }
            normal = normal * -1.0;
        }
        model.addFaceOnLoop(loopIds, normal);
    }
}

csg::Operand operandOf(const Model& model, Transform toWorld = Transform::identity()) {
    csg::Operand operand;
    operand.model = &model;
    operand.toWorld = toWorld;
    return operand;
}

// Runs one op and asserts structural success, returning the mesh volume.
double volumeOf(const csg::Operand& a, const csg::Operand& b, csg::Op op) {
    const csg::Result result = csg::apply(a, b, op);
    EXPECT_TRUE(result.ok);
    return csg::meshVolume(result.mesh);
}

// -- 1. meshVolume sanity ---------------------------------------------------

TEST(CsgTest, MeshVolumeOfHandBuiltUnitCubeIsOne) {
    csg::MeshSpec mesh;
    mesh.vertices = {
        Vec3{0.0, 0.0, 0.0}, Vec3{1.0, 0.0, 0.0}, Vec3{1.0, 1.0, 0.0}, Vec3{0.0, 1.0, 0.0},
        Vec3{0.0, 0.0, 1.0}, Vec3{1.0, 0.0, 1.0}, Vec3{1.0, 1.0, 1.0}, Vec3{0.0, 1.0, 1.0},
    };

    const std::vector<std::vector<int>> loops = {
        {0, 3, 2, 1},  // bottom, -z
        {4, 5, 6, 7},  // top, +z
        {0, 1, 5, 4},  // front, -y
        {3, 7, 6, 2},  // back, +y
        {0, 4, 7, 3},  // left, -x
        {1, 2, 6, 5},  // right, +x
    };
    const std::vector<Vec3> normals = {
        Vec3{0.0, 0.0, -1.0}, Vec3{0.0, 0.0, 1.0}, Vec3{0.0, -1.0, 0.0},
        Vec3{0.0, 1.0, 0.0},  Vec3{-1.0, 0.0, 0.0}, Vec3{1.0, 0.0, 0.0},
    };

    for (std::size_t i = 0; i < loops.size(); ++i) {
        csg::PolyFace face;
        face.loop = loops[i];
        face.normal = normals[i];
        mesh.faces.push_back(face);
    }

    EXPECT_NEAR(csg::meshVolume(mesh), 1.0, 1e-12);
}

// -- 2. Overlapping boxes ---------------------------------------------------

TEST(CsgTest, OverlappingBoxesUnionIsAnalyticVolume) {
    Model a;
    Model b;
    makeBox(a, Vec3{0.0, 0.0, 0.0}, Vec3{2.0, 2.0, 2.0});
    makeBox(b, Vec3{1.0, 1.0, 1.0}, Vec3{3.0, 3.0, 3.0});

    // 8 + 8 - 1 (the [1,2]^3 overlap counted once).
    EXPECT_NEAR(volumeOf(operandOf(a), operandOf(b), csg::Op::Union), 15.0, kVolumeTol);
}

TEST(CsgTest, OverlappingBoxesSubtractIsAnalyticVolumeBothWays) {
    Model a;
    Model b;
    makeBox(a, Vec3{0.0, 0.0, 0.0}, Vec3{2.0, 2.0, 2.0});
    makeBox(b, Vec3{1.0, 1.0, 1.0}, Vec3{3.0, 3.0, 3.0});

    EXPECT_NEAR(volumeOf(operandOf(a), operandOf(b), csg::Op::Subtract), 7.0, kVolumeTol);
    EXPECT_NEAR(volumeOf(operandOf(b), operandOf(a), csg::Op::Subtract), 7.0, kVolumeTol);
}

TEST(CsgTest, OverlappingBoxesIntersectIsAnalyticVolume) {
    Model a;
    Model b;
    makeBox(a, Vec3{0.0, 0.0, 0.0}, Vec3{2.0, 2.0, 2.0});
    makeBox(b, Vec3{1.0, 1.0, 1.0}, Vec3{3.0, 3.0, 3.0});

    EXPECT_NEAR(volumeOf(operandOf(a), operandOf(b), csg::Op::Intersect), 1.0, kVolumeTol);
}

// -- 3. Disjoint operands ---------------------------------------------------

TEST(CsgTest, DisjointBoxesUnionIsSumOfVolumes) {
    Model a;
    Model b;
    makeBox(a, Vec3{0.0, 0.0, 0.0}, Vec3{1.0, 1.0, 1.0});
    makeBox(b, Vec3{5.0, 5.0, 5.0}, Vec3{7.0, 7.0, 7.0});

    EXPECT_NEAR(volumeOf(operandOf(a), operandOf(b), csg::Op::Union), 1.0 + 8.0, kVolumeTol);
}

TEST(CsgTest, DisjointBoxesSubtractKeepsTargetUntouched) {
    Model a;
    Model b;
    makeBox(a, Vec3{0.0, 0.0, 0.0}, Vec3{1.0, 1.0, 1.0});
    makeBox(b, Vec3{5.0, 5.0, 5.0}, Vec3{7.0, 7.0, 7.0});

    EXPECT_NEAR(volumeOf(operandOf(a), operandOf(b), csg::Op::Subtract), 1.0, kVolumeTol);
}

TEST(CsgTest, DisjointBoxesIntersectIsEmptyButStillOk) {
    Model a;
    Model b;
    makeBox(a, Vec3{0.0, 0.0, 0.0}, Vec3{1.0, 1.0, 1.0});
    makeBox(b, Vec3{5.0, 5.0, 5.0}, Vec3{7.0, 7.0, 7.0});

    // An empty intersection is an ordinary success, not a structural failure.
    const csg::Result result = csg::apply(operandOf(a), operandOf(b), csg::Op::Intersect);
    EXPECT_TRUE(result.ok);
    EXPECT_TRUE(result.mesh.faces.empty());
    EXPECT_NEAR(csg::meshVolume(result.mesh), 0.0, kVolumeTol);
}

// -- 4. Containment ---------------------------------------------------------

TEST(CsgTest, ContainedBoxUnionKeepsOuterVolume) {
    Model a;
    Model b;
    makeBox(a, Vec3{0.0, 0.0, 0.0}, Vec3{3.0, 3.0, 3.0});
    makeBox(b, Vec3{1.0, 1.0, 1.0}, Vec3{2.0, 2.0, 2.0});

    EXPECT_NEAR(volumeOf(operandOf(a), operandOf(b), csg::Op::Union), 27.0, kVolumeTol);
}

TEST(CsgTest, ContainedBoxSubtractLeavesHollowSolid) {
    Model a;
    Model b;
    makeBox(a, Vec3{0.0, 0.0, 0.0}, Vec3{3.0, 3.0, 3.0});
    makeBox(b, Vec3{1.0, 1.0, 1.0}, Vec3{2.0, 2.0, 2.0});

    // The cavity shell is present with inward normals; the signed
    // divergence-theorem sum handles it directly (27 - 1).
    EXPECT_NEAR(volumeOf(operandOf(a), operandOf(b), csg::Op::Subtract), 26.0, kVolumeTol);
}

TEST(CsgTest, ContainedBoxIntersectIsInnerVolume) {
    Model a;
    Model b;
    makeBox(a, Vec3{0.0, 0.0, 0.0}, Vec3{3.0, 3.0, 3.0});
    makeBox(b, Vec3{1.0, 1.0, 1.0}, Vec3{2.0, 2.0, 2.0});

    EXPECT_NEAR(volumeOf(operandOf(a), operandOf(b), csg::Op::Intersect), 1.0, kVolumeTol);
}

// -- 5. Through hole --------------------------------------------------------

TEST(CsgTest, ThroughHoleSubtractRemovesPiercedColumn) {
    Model plate;
    Model pin;
    makeBox(plate, Vec3{0.0, 0.0, 0.0}, Vec3{4.0, 4.0, 1.0});
    makeBox(pin, Vec3{1.0, 1.0, -1.0}, Vec3{2.0, 2.0, 2.0});

    // 4*4*1 minus the 1*1*1 column the pin punches straight through.
    EXPECT_NEAR(volumeOf(operandOf(plate), operandOf(pin), csg::Op::Subtract), 15.0, kVolumeTol);
}

// -- 6. Coplanar shared face ------------------------------------------------

TEST(CsgTest, CoplanarSharedFaceUnionDropsInternalWall) {
    Model a;
    Model b;
    makeBox(a, Vec3{0.0, 0.0, 0.0}, Vec3{1.0, 1.0, 1.0});
    makeBox(b, Vec3{1.0, 0.0, 0.0}, Vec3{2.0, 1.0, 1.0});

    // The shared x = 1 wall is interior to the union: coplanar routing must
    // drop BOTH copies (keeping either one would land at 4/3 or 2/3, not 2).
    EXPECT_NEAR(volumeOf(operandOf(a), operandOf(b), csg::Op::Union), 2.0, kVolumeTol);
}

TEST(CsgTest, CoplanarSharedFaceIntersectIsEmpty) {
    Model a;
    Model b;
    makeBox(a, Vec3{0.0, 0.0, 0.0}, Vec3{1.0, 1.0, 1.0});
    makeBox(b, Vec3{1.0, 0.0, 0.0}, Vec3{2.0, 1.0, 1.0});

    const csg::Result result = csg::apply(operandOf(a), operandOf(b), csg::Op::Intersect);
    ASSERT_TRUE(result.ok);
    EXPECT_NEAR(csg::meshVolume(result.mesh), 0.0, kVolumeTol);
}

TEST(CsgTest, CoplanarSharedFaceSubtractKeepsTarget) {
    Model a;
    Model b;
    makeBox(a, Vec3{0.0, 0.0, 0.0}, Vec3{1.0, 1.0, 1.0});
    makeBox(b, Vec3{1.0, 0.0, 0.0}, Vec3{2.0, 1.0, 1.0});

    EXPECT_NEAR(volumeOf(operandOf(a), operandOf(b), csg::Op::Subtract), 1.0, kVolumeTol);
}

// -- 7. Operand transforms --------------------------------------------------

TEST(CsgTest, TranslatedOperandMatchesPrePositionedBox) {
    Model a;
    Model placed;
    Model atOrigin;
    makeBox(a, Vec3{0.0, 0.0, 0.0}, Vec3{2.0, 2.0, 2.0});
    makeBox(placed, Vec3{1.0, 1.0, 1.0}, Vec3{3.0, 3.0, 3.0});
    makeBox(atOrigin, Vec3{0.0, 0.0, 0.0}, Vec3{2.0, 2.0, 2.0});

    const Transform toWorld = Transform::translation(Vec3{1.0, 1.0, 1.0});

    for (const csg::Op op : {csg::Op::Union, csg::Op::Subtract, csg::Op::Intersect}) {
        const double placedVolume = volumeOf(operandOf(a), operandOf(placed), op);
        const double transformedVolume = volumeOf(operandOf(a), operandOf(atOrigin, toWorld), op);
        EXPECT_NEAR(transformedVolume, placedVolume, kVolumeTol);
    }
}

TEST(CsgTest, MirroredOperandStillYieldsPositiveVolumes) {
    Model a;
    Model atOrigin;
    makeBox(a, Vec3{0.0, 0.0, 0.0}, Vec3{2.0, 2.0, 2.0});
    makeBox(atOrigin, Vec3{0.0, 0.0, 0.0}, Vec3{2.0, 2.0, 2.0});

    const Transform toWorld = Transform::translation(Vec3{3.0, 1.0, 1.0})
                                  .composed(Transform::scaling(Vec3{0.0, 0.0, 0.0}, -1.0, 1.0, 1.0));

    EXPECT_NEAR(volumeOf(operandOf(a), operandOf(atOrigin, toWorld), csg::Op::Union), 15.0, kVolumeTol);
    EXPECT_NEAR(volumeOf(operandOf(a), operandOf(atOrigin, toWorld), csg::Op::Subtract), 7.0, kVolumeTol);
    EXPECT_NEAR(volumeOf(operandOf(a), operandOf(atOrigin, toWorld), csg::Op::Intersect), 1.0, kVolumeTol);
}

// -- 8. Provenance ----------------------------------------------------------

TEST(CsgTest, EveryResultFaceCarriesResolvableProvenance) {
    Model a;
    Model b;
    makeBox(a, Vec3{0.0, 0.0, 0.0}, Vec3{2.0, 2.0, 2.0});
    makeBox(b, Vec3{1.0, 1.0, 1.0}, Vec3{3.0, 3.0, 3.0});

    const Model* models[2] = {&a, &b};
    for (const csg::Op op : {csg::Op::Union, csg::Op::Subtract, csg::Op::Intersect}) {
        const csg::Result result = csg::apply(operandOf(a), operandOf(b), op);
        ASSERT_TRUE(result.ok);
        ASSERT_FALSE(result.mesh.faces.empty());

        for (const csg::PolyFace& face : result.mesh.faces) {
            ASSERT_GE(face.operandIndex, 0);
            ASSERT_LE(face.operandIndex, 1);
            EXPECT_NE(models[face.operandIndex]->face(face.sourceFaceId), nullptr);

            // Every loop index must address a real vertex, and the loop's own
            // winding must agree with the reported normal.
            ASSERT_GE(face.loop.size(), 3u);
            for (int index : face.loop) {
                ASSERT_GE(index, 0);
                ASSERT_LT(static_cast<std::size_t>(index), result.mesh.vertices.size());
            }
            const Vec3& v0 = result.mesh.vertices[static_cast<std::size_t>(face.loop[0])];
            const Vec3& v1 = result.mesh.vertices[static_cast<std::size_t>(face.loop[1])];
            const Vec3& v2 = result.mesh.vertices[static_cast<std::size_t>(face.loop[2])];
            EXPECT_GT(dot(cross(v1 - v0, v2 - v0), face.normal), 0.0);
        }
    }
}

// -- 9. Symmetry ------------------------------------------------------------

TEST(CsgTest, UnionVolumeIsOrderIndependent) {
    Model a;
    Model b;
    makeBox(a, Vec3{0.0, 0.0, 0.0}, Vec3{2.0, 2.0, 2.0});
    makeBox(b, Vec3{1.0, 1.0, 1.0}, Vec3{3.0, 3.0, 3.0});

    const double forward = volumeOf(operandOf(a), operandOf(b), csg::Op::Union);
    const double reverse = volumeOf(operandOf(b), operandOf(a), csg::Op::Union);
    EXPECT_NEAR(forward, reverse, kVolumeTol);

    const double forwardIntersect = volumeOf(operandOf(a), operandOf(b), csg::Op::Intersect);
    const double reverseIntersect = volumeOf(operandOf(b), operandOf(a), csg::Op::Intersect);
    EXPECT_NEAR(forwardIntersect, reverseIntersect, kVolumeTol);
}

// Every loop must be a SIMPLE polygon: at least 3 indices, all addressing a
// real vertex, none repeated.
void expectSimpleLoops(const csg::MeshSpec& mesh) {
    for (const csg::PolyFace& face : mesh.faces) {
        ASSERT_GE(face.loop.size(), 3u);
        std::set<int> seen;
        for (int index : face.loop) {
            ASSERT_GE(index, 0);
            ASSERT_LT(static_cast<std::size_t>(index), mesh.vertices.size());
            EXPECT_TRUE(seen.insert(index).second) << "vertex " << index << " repeats in a loop";
        }
    }
}

// -- 10. Merge invariance ---------------------------------------------------

TEST(CsgTest, MergeCoplanarFacesPreservesVolumeAndSimplifiesEveryOp) {
    Model a;
    Model b;
    makeBox(a, Vec3{0.0, 0.0, 0.0}, Vec3{2.0, 2.0, 2.0});
    makeBox(b, Vec3{1.0, 1.0, 1.0}, Vec3{3.0, 3.0, 3.0});

    for (const csg::Op op : {csg::Op::Union, csg::Op::Subtract, csg::Op::Intersect}) {
        const csg::Result unmerged = csg::apply(operandOf(a), operandOf(b), op);
        ASSERT_TRUE(unmerged.ok);
        ASSERT_FALSE(unmerged.mesh.faces.empty());

        const csg::MeshSpec merged = csg::mergeCoplanarFaces(unmerged.mesh);
        EXPECT_NEAR(csg::meshVolume(merged), csg::meshVolume(unmerged.mesh), kVolumeTol);
        EXPECT_LT(merged.faces.size(), unmerged.mesh.faces.size());
        EXPECT_TRUE(csg::meshIsWatertight(merged));
        expectSimpleLoops(merged);
    }
}

TEST(CsgTest, MergedUnionOfOverlappingBoxesCollapsesToAtMostTwelveFaces) {
    Model a;
    Model b;
    makeBox(a, Vec3{0.0, 0.0, 0.0}, Vec3{2.0, 2.0, 2.0});
    makeBox(b, Vec3{1.0, 1.0, 1.0}, Vec3{3.0, 3.0, 3.0});

    const csg::Result unioned = csg::apply(operandOf(a), operandOf(b), csg::Op::Union);
    ASSERT_TRUE(unioned.ok);
    const csg::MeshSpec merged = csg::mergeCoplanarFaces(unioned.mesh);

    // Each box side survives as at most one simple polygon; a ceiling not an
    // exact count, since the BSP's exact fragmentation isn't the point here.
    EXPECT_LE(merged.faces.size(), 12u);
    EXPECT_NEAR(csg::meshVolume(merged), 15.0, kVolumeTol);
}

// -- 11. Through hole (annulus faces exercise hole splitting) ---------------

TEST(CsgTest, ThroughHoleSubtractMergesToSimpleWatertightAnnuli) {
    Model plate;
    Model pin;
    makeBox(plate, Vec3{0.0, 0.0, 0.0}, Vec3{4.0, 4.0, 1.0});
    makeBox(pin, Vec3{1.0, 1.0, -1.0}, Vec3{2.0, 2.0, 2.0});

    const csg::Result pierced = csg::apply(operandOf(plate), operandOf(pin), csg::Op::Subtract);
    ASSERT_TRUE(pierced.ok);
    const csg::MeshSpec merged = csg::mergeCoplanarFaces(pierced.mesh);

    // The top and bottom faces come out as annuli (a square with a square
    // hole). Whether they are split into simple pieces or the group falls back
    // to its fragments, these three guarantees must hold either way.
    EXPECT_NEAR(csg::meshVolume(merged), 15.0, kVolumeTol);
    EXPECT_TRUE(csg::meshIsWatertight(merged));
    expectSimpleLoops(merged);
}

// -- 12. Coplanar shared face -----------------------------------------------

TEST(CsgTest, CoplanarSharedFaceUnionMergesToWatertightSolid) {
    Model a;
    Model b;
    makeBox(a, Vec3{0.0, 0.0, 0.0}, Vec3{1.0, 1.0, 1.0});
    makeBox(b, Vec3{1.0, 0.0, 0.0}, Vec3{2.0, 1.0, 1.0});

    const csg::Result unioned = csg::apply(operandOf(a), operandOf(b), csg::Op::Union);
    ASSERT_TRUE(unioned.ok);
    const csg::MeshSpec merged = csg::mergeCoplanarFaces(unioned.mesh);

    EXPECT_NEAR(csg::meshVolume(merged), 2.0, kVolumeTol);
    EXPECT_TRUE(csg::meshIsWatertight(merged));
    expectSimpleLoops(merged);
}

// -- 13. meshIsWatertight itself --------------------------------------------

TEST(CsgTest, MeshIsWatertightRejectsAnOpenShellAndAcceptsAClosedOne) {
    Model a;
    Model b;
    makeBox(a, Vec3{0.0, 0.0, 0.0}, Vec3{2.0, 2.0, 2.0});
    makeBox(b, Vec3{1.0, 1.0, 1.0}, Vec3{3.0, 3.0, 3.0});

    const csg::Result unioned = csg::apply(operandOf(a), operandOf(b), csg::Op::Union);
    ASSERT_TRUE(unioned.ok);
    csg::MeshSpec merged = csg::mergeCoplanarFaces(unioned.mesh);
    ASSERT_TRUE(csg::meshIsWatertight(merged));

    // Punch one face out: the mesh is judged verbatim, so the hole shows.
    merged.faces.pop_back();
    EXPECT_FALSE(csg::meshIsWatertight(merged));

    // An empty mesh is vacuously watertight.
    EXPECT_TRUE(csg::meshIsWatertight(csg::MeshSpec{}));
}

// -- 14. Outer Shell --------------------------------------------------------

TEST(CsgTest, OuterShellOfTwoSolidBoxesMatchesTheirUnion) {
    Model a;
    Model b;
    makeBox(a, Vec3{0.0, 0.0, 0.0}, Vec3{2.0, 2.0, 2.0});
    makeBox(b, Vec3{1.0, 1.0, 1.0}, Vec3{3.0, 3.0, 3.0});

    const csg::Result shell = csg::outerShell(operandOf(a), operandOf(b));
    ASSERT_TRUE(shell.ok);

    // Two solid boxes have no enclosed void, so nothing may be dropped.
    EXPECT_NEAR(csg::meshVolume(shell.mesh), 15.0, kVolumeTol);
    EXPECT_TRUE(csg::meshIsWatertight(shell.mesh));
    expectSimpleLoops(shell.mesh);
}

TEST(CsgTest, OuterShellDropsCavityWallsAndAnEnclosedFloatingSolid) {
    // A hollow cube (solid_test's reversed-inner-box pattern) plus a small
    // solid box floating in its cavity, touching nothing.
    Model a;
    makeBox(a, Vec3{0.0, 0.0, 0.0}, Vec3{4.0, 4.0, 4.0}, /*outward=*/true);
    makeBox(a, Vec3{1.0, 1.0, 1.0}, Vec3{3.0, 3.0, 3.0}, /*outward=*/false);
    Model b;
    makeBox(b, Vec3{1.75, 1.75, 1.75}, Vec3{2.25, 2.25, 2.25});

    const csg::Result unioned = csg::apply(operandOf(a), operandOf(b), csg::Op::Union);
    ASSERT_TRUE(unioned.ok);
    const csg::MeshSpec mergedUnion = csg::mergeCoplanarFaces(unioned.mesh);
    // Cavity walls (64 - 8) plus the floating box (0.5^3), both retained.
    EXPECT_NEAR(csg::meshVolume(mergedUnion), 56.0 + 0.125, kVolumeTol);

    const csg::Result shell = csg::outerShell(operandOf(a), operandOf(b));
    ASSERT_TRUE(shell.ok);

    // Everything enclosed goes: the cavity shell AND the floating solid, both
    // of which are contained in another shell.
    EXPECT_NEAR(csg::meshVolume(shell.mesh), 64.0, kVolumeTol);
    EXPECT_TRUE(csg::meshIsWatertight(shell.mesh));
    expectSimpleLoops(shell.mesh);
    EXPECT_LT(shell.mesh.faces.size(), mergedUnion.faces.size());
}

// -- 15. Split --------------------------------------------------------------

TEST(CsgTest, SplitOfOverlappingBoxesYieldsThreeWatertightPieces) {
    Model a;
    Model b;
    makeBox(a, Vec3{0.0, 0.0, 0.0}, Vec3{2.0, 2.0, 2.0});
    makeBox(b, Vec3{1.0, 1.0, 1.0}, Vec3{3.0, 3.0, 3.0});

    const csg::SplitResult pieces = csg::split(operandOf(a), operandOf(b));
    ASSERT_TRUE(pieces.ok);

    EXPECT_NEAR(csg::meshVolume(pieces.aMinusB), 7.0, kVolumeTol);
    EXPECT_NEAR(csg::meshVolume(pieces.bMinusA), 7.0, kVolumeTol);
    EXPECT_NEAR(csg::meshVolume(pieces.aIntersectB), 1.0, kVolumeTol);

    for (const csg::MeshSpec* piece : {&pieces.aMinusB, &pieces.bMinusA, &pieces.aIntersectB}) {
        EXPECT_TRUE(csg::meshIsWatertight(*piece));
        expectSimpleLoops(*piece);
    }
}

TEST(CsgTest, SplitOfDisjointBoxesKeepsBothOperandsAndAnEmptyIntersection) {
    Model a;
    Model b;
    makeBox(a, Vec3{0.0, 0.0, 0.0}, Vec3{1.0, 1.0, 1.0});
    makeBox(b, Vec3{5.0, 5.0, 5.0}, Vec3{7.0, 7.0, 7.0});

    const csg::SplitResult pieces = csg::split(operandOf(a), operandOf(b));
    ASSERT_TRUE(pieces.ok);

    EXPECT_NEAR(csg::meshVolume(pieces.aMinusB), 1.0, kVolumeTol);
    EXPECT_NEAR(csg::meshVolume(pieces.bMinusA), 8.0, kVolumeTol);
    // An empty overlap is an ordinary success, not a failure.
    EXPECT_TRUE(pieces.aIntersectB.faces.empty());
    EXPECT_NEAR(csg::meshVolume(pieces.aIntersectB), 0.0, kVolumeTol);
}

// -- 16. Determinism --------------------------------------------------------

TEST(CsgTest, MergeCoplanarFacesIsDeterministic) {
    Model plate;
    Model pin;
    makeBox(plate, Vec3{0.0, 0.0, 0.0}, Vec3{4.0, 4.0, 1.0});
    makeBox(pin, Vec3{1.0, 1.0, -1.0}, Vec3{2.0, 2.0, 2.0});

    // The through-hole case: it exercises healing, cancellation AND the hole
    // split, so every stage that could pick an order is covered.
    const csg::Result pierced = csg::apply(operandOf(plate), operandOf(pin), csg::Op::Subtract);
    ASSERT_TRUE(pierced.ok);

    const csg::MeshSpec first = csg::mergeCoplanarFaces(pierced.mesh);
    const csg::MeshSpec second = csg::mergeCoplanarFaces(pierced.mesh);

    ASSERT_EQ(first.vertices.size(), second.vertices.size());
    for (std::size_t i = 0; i < first.vertices.size(); ++i) {
        EXPECT_EQ(first.vertices[i].x, second.vertices[i].x);
        EXPECT_EQ(first.vertices[i].y, second.vertices[i].y);
        EXPECT_EQ(first.vertices[i].z, second.vertices[i].z);
    }

    ASSERT_EQ(first.faces.size(), second.faces.size());
    for (std::size_t i = 0; i < first.faces.size(); ++i) {
        EXPECT_EQ(first.faces[i].loop, second.faces[i].loop);
        EXPECT_EQ(first.faces[i].operandIndex, second.faces[i].operandIndex);
        EXPECT_EQ(first.faces[i].sourceFaceId, second.faces[i].sourceFaceId);
        EXPECT_EQ(first.faces[i].normal.x, second.faces[i].normal.x);
        EXPECT_EQ(first.faces[i].normal.y, second.faces[i].normal.y);
        EXPECT_EQ(first.faces[i].normal.z, second.faces[i].normal.z);
    }
}

// -- Structural failure -----------------------------------------------------

TEST(CsgTest, NullOrFacelessOperandIsStructuralFailure) {
    Model a;
    makeBox(a, Vec3{0.0, 0.0, 0.0}, Vec3{1.0, 1.0, 1.0});
    Model empty;

    csg::Operand nullOperand;  // model stays nullptr
    EXPECT_FALSE(csg::apply(operandOf(a), nullOperand, csg::Op::Union).ok);
    EXPECT_FALSE(csg::apply(nullOperand, operandOf(a), csg::Op::Union).ok);
    EXPECT_FALSE(csg::apply(operandOf(a), operandOf(empty), csg::Op::Subtract).ok);
    EXPECT_FALSE(csg::apply(operandOf(empty), operandOf(a), csg::Op::Intersect).ok);
}

}  // namespace
