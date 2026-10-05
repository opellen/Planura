#include "io/plr_writer.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <tuple>
#include <utility>
#include <vector>

#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>

#include <ordo/core/kernel.h>

#include "agent/annotation_store.h"
#include "agent/axes_store.h"
#include "agent/events.h"
#include "agent/fog_store.h"
#include "agent/geometry_api.h"
#include "agent/guide_store.h"
#include "agent/material_repository.h"
#include "agent/section_store.h"
#include "agent/shadow_store.h"
#include "agent/style_store.h"
#include "agent/tag_store.h"
#include "io/plr_format.h"

#include <geo/entity.h>
#include <geo/model.h>
#include <geo/scene.h>
#include <geo/vec3.h>

#include <gtest/gtest.h>

namespace {

using ordo::core::Kernel;
using plnr::agent::AnnotationStore;
using plnr::agent::AxesStore;
using plnr::agent::FogStore;
using plnr::agent::GeometryApi;
using plnr::agent::GuideStore;
using plnr::agent::kAnnotationStoreName;
using plnr::agent::kAxesStoreName;
using plnr::agent::kFogStoreName;
using plnr::agent::kGeometryApiName;
using plnr::agent::kGuideStoreName;
using plnr::agent::kMaterialRepositoryName;
using plnr::agent::kSectionStoreName;
using plnr::agent::kShadowStoreName;
using plnr::agent::kStyleStoreName;
using plnr::agent::kTagStoreName;
using plnr::agent::MaterialRepository;
using plnr::agent::SectionStore;
using plnr::agent::ShadowStore;
using plnr::agent::StyleStore;
using plnr::agent::TagStore;
using plnr::events::EntityRef;
using plnr::geo::Definition;
using plnr::geo::EntityKind;
using plnr::geo::Id;
using plnr::geo::Instance;
using plnr::geo::Model;
using plnr::geo::Vec3;
using plnr::geo::Vertex;
using plnr::io::CameraState;
using plnr::io::DocumentMeta;

double toD(Id id) {
    return static_cast<double>(id);
}

QJsonArray vecJson(double x, double y, double z) {
    return QJsonArray{x, y, z};
}

// Finds the edge connecting va/vb (either direction) via public Model API
// only -- independent of allocation order, used to recover a known
// rectangle corner-pair's edge id for the golden test's expected JSON.
Id findEdgeBetween(const Model& model, Id va, Id vb) {
    for (const auto& [id, edge] : model.edges()) {
        const auto* h0 = model.halfEdge(edge.halfEdges[0]);
        const auto* h1 = model.halfEdge(edge.halfEdges[1]);
        if (h0 == nullptr || h1 == nullptr) continue;
        if ((h0->origin == va && h1->origin == vb) || (h0->origin == vb && h1->origin == va)) {
            return id;
        }
    }
    return plnr::geo::kInvalidId;
}

// Wires a kernel with every agent writeDocument() walks and builds a
// full-coverage document. Ids are resolved from live queries, not assumed
// from allocation order -- makeGroup's own header comment: ids do NOT survive a group move.
class PlrWriterTest : public ::testing::Test {
protected:
    void SetUp() override {
        kernel.registerAgent(std::make_shared<GeometryApi>());
        kernel.registerAgent(std::make_shared<TagStore>());
        kernel.registerAgent(std::make_shared<GuideStore>());
        kernel.registerAgent(std::make_shared<AnnotationStore>());
        kernel.registerAgent(std::make_shared<SectionStore>());
        kernel.registerAgent(std::make_shared<AxesStore>());
        kernel.registerAgent(std::make_shared<MaterialRepository>());
        kernel.registerAgent(std::make_shared<StyleStore>());
        kernel.registerAgent(std::make_shared<ShadowStore>());
        kernel.registerAgent(std::make_shared<FogStore>());
        geometry = kernel.agentAs<GeometryApi>(kGeometryApiName);
        tags = kernel.agentAs<TagStore>(kTagStoreName);
        guides = kernel.agentAs<GuideStore>(kGuideStoreName);
        annotations = kernel.agentAs<AnnotationStore>(kAnnotationStoreName);
        sections = kernel.agentAs<SectionStore>(kSectionStoreName);
        axes = kernel.agentAs<AxesStore>(kAxesStoreName);
        materials = kernel.agentAs<MaterialRepository>(kMaterialRepositoryName);
        style = kernel.agentAs<StyleStore>(kStyleStoreName);
        shadow = kernel.agentAs<ShadowStore>(kShadowStoreName);
        fog = kernel.agentAs<FogStore>(kFogStoreName);

        // -- Root mesh: one face (rectangle) + one wire edge -----------------
        ASSERT_TRUE(geometry->addRectangle({0, 0, 0}, {4, 3, 0}));
        ASSERT_TRUE(geometry->addEdge({10, 0, 0}, {10, 5, 0}).created);

        v00 = geometry->model().findVertex({0, 0, 0});
        v40 = geometry->model().findVertex({4, 0, 0});
        v43 = geometry->model().findVertex({4, 3, 0});
        v03 = geometry->model().findVertex({0, 3, 0});
        vWireA = geometry->model().findVertex({10, 0, 0});
        vWireB = geometry->model().findVertex({10, 5, 0});
        ASSERT_NE(v00, nullptr);
        ASSERT_NE(v40, nullptr);
        ASSERT_NE(v43, nullptr);
        ASSERT_NE(v03, nullptr);
        ASSERT_NE(vWireA, nullptr);
        ASSERT_NE(vWireB, nullptr);
        ASSERT_EQ(geometry->model().faces().size(), 1u);
        rootFaceId = geometry->model().faces().begin()->first;
        wireEdgeId = findEdgeBetween(geometry->model(), vWireA->id, vWireB->id);
        ASSERT_NE(wireEdgeId, plnr::geo::kInvalidId);

        // -- Group definition with its own face + one instance of it --------
        ASSERT_TRUE(geometry->addRectangle({20, 0, 0}, {22, 2, 0}));
        Id groupSourceFaceId = plnr::geo::kInvalidId;
        for (const auto& [id, face] : geometry->model().faces()) {
            (void)face;
            if (id != rootFaceId) {
                groupSourceFaceId = id;
                break;
            }
        }
        ASSERT_NE(groupSourceFaceId, plnr::geo::kInvalidId);

        newInstanceId = geometry->makeGroup({EntityRef{EntityKind::Face, groupSourceFaceId}}, false, "MyGroup");
        ASSERT_NE(newInstanceId, plnr::geo::kInvalidId);

        const Definition& rootDef = geometry->scene().root();
        const Instance* placedInstance = nullptr;
        for (const Instance& child : rootDef.children) {
            if (child.id == newInstanceId) {
                placedInstance = &child;
                break;
            }
        }
        ASSERT_NE(placedInstance, nullptr);
        groupDefId = placedInstance->definitionId;

        const Definition* groupDef = geometry->scene().definition(groupDefId);
        ASSERT_NE(groupDef, nullptr);
        ASSERT_EQ(groupDef->model.faces().size(), 1u);
        groupFaceId = groupDef->model.faces().begin()->first;
        gv20_0 = groupDef->model.findVertex({20, 0, 0});
        gv22_0 = groupDef->model.findVertex({22, 0, 0});
        gv22_2 = groupDef->model.findVertex({22, 2, 0});
        gv20_2 = groupDef->model.findVertex({20, 2, 0});
        ASSERT_NE(gv20_0, nullptr);
        ASSERT_NE(gv22_0, nullptr);
        ASSERT_NE(gv22_2, nullptr);
        ASSERT_NE(gv20_2, nullptr);

        // -- Tags: a second tag, with one assignment (on the group instance) -
        wallsTagId = tags->createTag("Walls");
        ASSERT_TRUE(tags->assignTag({EntityRef{EntityKind::Instance, newInstanceId}}, wallsTagId));

        // -- Hidden: the wire edge -------------------------------------------
        ASSERT_TRUE(geometry->setHidden({EntityRef{EntityKind::Edge, wireEdgeId}}, true));

        // -- Guides: one line + one point ------------------------------------
        lineGuideId = guides->addGuideLine({0, 0, 5}, {1, 0, 0});
        pointGuideId = guides->addGuidePoint({2, 2, 2});

        // -- Annotations: one dimension, one screen text, one leader text ----
        dimId = annotations->addDimension(v00->id, v40->id, {0, 0, 1}, 0.5, geometry->model());
        screenTextId = annotations->addScreenText(100.0, 200.0, "Hello");
        leaderTextId = annotations->addLeaderText({1, 1, 0}, EntityRef{EntityKind::Vertex, v00->id}, "Leader");

        // -- Section plane: one, active ---------------------------------------
        sectionId = sections->addPlane({0, 0, 1}, {0, 0, 1}, "Cut A");
        ASSERT_TRUE(sections->setActive(sectionId, true));

        // -- Axes: non-default frame ------------------------------------------
        ASSERT_TRUE(axes->set({1, 2, 3}, {0, 1, 0}, {1, 0, 0}));
    }

    Kernel kernel;
    std::shared_ptr<GeometryApi> geometry;
    std::shared_ptr<TagStore> tags;
    std::shared_ptr<GuideStore> guides;
    std::shared_ptr<AnnotationStore> annotations;
    std::shared_ptr<SectionStore> sections;
    std::shared_ptr<AxesStore> axes;
    std::shared_ptr<MaterialRepository> materials;
    std::shared_ptr<StyleStore> style;
    std::shared_ptr<ShadowStore> shadow;
    std::shared_ptr<FogStore> fog;

    const Vertex* v00 = nullptr;
    const Vertex* v40 = nullptr;
    const Vertex* v43 = nullptr;
    const Vertex* v03 = nullptr;
    const Vertex* vWireA = nullptr;
    const Vertex* vWireB = nullptr;
    Id rootFaceId = 0;
    Id wireEdgeId = 0;
    Id newInstanceId = 0;
    Id groupDefId = 0;
    Id groupFaceId = 0;
    const Vertex* gv20_0 = nullptr;
    const Vertex* gv22_0 = nullptr;
    const Vertex* gv22_2 = nullptr;
    const Vertex* gv20_2 = nullptr;
    std::uint64_t wallsTagId = 0;
    Id lineGuideId = 0;
    Id pointGuideId = 0;
    Id dimId = 0;
    Id screenTextId = 0;
    Id leaderTextId = 0;
    Id sectionId = 0;

    // Projection set to a non-default value (not just the Perspective
    // default) so GoldenDocumentMatchesExpectedJson below actually
    // exercises the new field's write path.
    CameraState camera{{5, 6, 7}, 45.0, 30.0, 12.5, 35.0, plnr::events::Projection::Parallel};
    DocumentMeta meta{QStringLiteral("test"), QStringLiteral("2026-08-22T00:00:00Z"), QStringLiteral("in")};
};

TEST_F(PlrWriterTest, GoldenDocumentMatchesExpectedJson) {
    const QJsonDocument doc =
        plnr::io::writeDocument(*geometry, *tags, *guides, *annotations, *sections, *axes, *materials, *style,
                                *shadow, *fog, camera, meta);
    ASSERT_TRUE(doc.isObject());
    const QJsonObject root = doc.object();

    // -- top-level scalars ---------------------------------------------------
    const QString expectedFormat =
        QString::fromUtf8(plnr::io::kFormatName.data(), static_cast<qsizetype>(plnr::io::kFormatName.size()));
    EXPECT_EQ(root.value("format").toString(), expectedFormat);
    EXPECT_EQ(root.value("formatVersion").toInt(), plnr::io::kFormatVersion);
    EXPECT_TRUE(root.value("curves").isNull());
    EXPECT_TRUE(root.value("materials").isNull());  // this fixture never creates a material
    EXPECT_FALSE(root.contains("materialAssignments"));  // omitted entirely alongside a null materials

    // -- meta -----------------------------------------------------------------
    QJsonObject expectedMeta;
    expectedMeta["appVersion"] = meta.appVersion;
    expectedMeta["savedAt"] = meta.savedAt;
    expectedMeta["units"] = meta.units;
    EXPECT_EQ(root.value("meta").toObject(), expectedMeta);

    // -- root mesh: vertices/edges sorted by id, face loop verbatim ----------
    const Id e1 = findEdgeBetween(geometry->model(), v00->id, v40->id);
    const Id e2 = findEdgeBetween(geometry->model(), v40->id, v43->id);
    const Id e3 = findEdgeBetween(geometry->model(), v43->id, v03->id);
    const Id e4 = findEdgeBetween(geometry->model(), v03->id, v00->id);
    ASSERT_NE(e1, plnr::geo::kInvalidId);
    ASSERT_NE(e2, plnr::geo::kInvalidId);
    ASSERT_NE(e3, plnr::geo::kInvalidId);
    ASSERT_NE(e4, plnr::geo::kInvalidId);

    std::vector<std::pair<Id, Vec3>> rootVerts = {
        {v00->id, v00->pos}, {v40->id, v40->pos},       {v43->id, v43->pos},
        {v03->id, v03->pos}, {vWireA->id, vWireA->pos}, {vWireB->id, vWireB->pos},
    };
    std::sort(rootVerts.begin(), rootVerts.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    QJsonArray expectedRootVertices;
    for (const auto& [id, pos] : rootVerts) {
        expectedRootVertices.append(QJsonArray{toD(id), pos.x, pos.y, pos.z});
    }

    std::vector<std::tuple<Id, Id, Id>> rootEdges = {
        {e1, v00->id, v40->id},
        {e2, v40->id, v43->id},
        {e3, v43->id, v03->id},
        {e4, v03->id, v00->id},
        {wireEdgeId, vWireA->id, vWireB->id},
    };
    std::sort(rootEdges.begin(), rootEdges.end(),
              [](const auto& a, const auto& b) { return std::get<0>(a) < std::get<0>(b); });
    QJsonArray expectedRootEdges;
    for (const auto& [id, va, vb] : rootEdges) {
        expectedRootEdges.append(QJsonArray{toD(id), toD(va), toD(vb)});
    }

    QJsonArray expectedRootLoop;
    for (Id vertexId : geometry->model().faceVertexLoop(rootFaceId)) {
        expectedRootLoop.append(toD(vertexId));
    }
    QJsonObject expectedRootFace;
    expectedRootFace["id"] = toD(rootFaceId);
    expectedRootFace["loop"] = expectedRootLoop;

    QJsonObject expectedRootMesh;
    expectedRootMesh["vertices"] = expectedRootVertices;
    expectedRootMesh["edges"] = expectedRootEdges;
    expectedRootMesh["faces"] = QJsonArray{expectedRootFace};

    // -- group mesh -------------------------------------------------------
    const Definition* groupDef = geometry->scene().definition(groupDefId);
    ASSERT_NE(groupDef, nullptr);
    const Id ge1 = findEdgeBetween(groupDef->model, gv20_0->id, gv22_0->id);
    const Id ge2 = findEdgeBetween(groupDef->model, gv22_0->id, gv22_2->id);
    const Id ge3 = findEdgeBetween(groupDef->model, gv22_2->id, gv20_2->id);
    const Id ge4 = findEdgeBetween(groupDef->model, gv20_2->id, gv20_0->id);
    ASSERT_NE(ge1, plnr::geo::kInvalidId);
    ASSERT_NE(ge2, plnr::geo::kInvalidId);
    ASSERT_NE(ge3, plnr::geo::kInvalidId);
    ASSERT_NE(ge4, plnr::geo::kInvalidId);

    std::vector<std::pair<Id, Vec3>> groupVerts = {
        {gv20_0->id, gv20_0->pos},
        {gv22_0->id, gv22_0->pos},
        {gv22_2->id, gv22_2->pos},
        {gv20_2->id, gv20_2->pos},
    };
    std::sort(groupVerts.begin(), groupVerts.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    QJsonArray expectedGroupVertices;
    for (const auto& [id, pos] : groupVerts) {
        expectedGroupVertices.append(QJsonArray{toD(id), pos.x, pos.y, pos.z});
    }

    std::vector<std::tuple<Id, Id, Id>> groupEdges = {
        {ge1, gv20_0->id, gv22_0->id},
        {ge2, gv22_0->id, gv22_2->id},
        {ge3, gv22_2->id, gv20_2->id},
        {ge4, gv20_2->id, gv20_0->id},
    };
    std::sort(groupEdges.begin(), groupEdges.end(),
              [](const auto& a, const auto& b) { return std::get<0>(a) < std::get<0>(b); });
    QJsonArray expectedGroupEdges;
    for (const auto& [id, va, vb] : groupEdges) {
        expectedGroupEdges.append(QJsonArray{toD(id), toD(va), toD(vb)});
    }

    QJsonArray expectedGroupLoop;
    for (Id vertexId : groupDef->model.faceVertexLoop(groupFaceId)) {
        expectedGroupLoop.append(toD(vertexId));
    }
    QJsonObject expectedGroupFace;
    expectedGroupFace["id"] = toD(groupFaceId);
    expectedGroupFace["loop"] = expectedGroupLoop;

    QJsonObject expectedGroupMesh;
    expectedGroupMesh["vertices"] = expectedGroupVertices;
    expectedGroupMesh["edges"] = expectedGroupEdges;
    expectedGroupMesh["faces"] = QJsonArray{expectedGroupFace};

    // definitions: root then group (sorted by id); root's instance transform
    // flattens as [col0, col1, col2, t] -- makeGroup always places identity,
    // so the non-identity case is covered separately below (InstanceTransformFlattensColumnsThenTranslation).
    ASSERT_LT(plnr::geo::kRootDefinitionId, groupDefId);

    QJsonObject expectedInstance;
    expectedInstance["id"] = toD(newInstanceId);
    expectedInstance["definitionId"] = toD(groupDefId);
    expectedInstance["name"] = QStringLiteral("MyGroup");
    expectedInstance["transform"] = QJsonArray{1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0};

    QJsonObject expectedRootDef;
    expectedRootDef["id"] = toD(plnr::geo::kRootDefinitionId);
    expectedRootDef["name"] = QStringLiteral("Model");  // geo::Scene::Scene()'s own default root name
    expectedRootDef["isGroup"] = false;
    expectedRootDef["mesh"] = expectedRootMesh;
    expectedRootDef["instances"] = QJsonArray{expectedInstance};

    QJsonObject expectedGroupDef;
    expectedGroupDef["id"] = toD(groupDefId);
    expectedGroupDef["name"] = QStringLiteral("MyGroup");
    expectedGroupDef["isGroup"] = true;
    expectedGroupDef["mesh"] = expectedGroupMesh;
    expectedGroupDef["instances"] = QJsonArray{};

    const QJsonArray expectedDefinitions{expectedRootDef, expectedGroupDef};
    EXPECT_EQ(root.value("definitions").toArray(), expectedDefinitions);

    // -- tags / tagAssignments / hidden --------------------------------------
    QJsonObject untaggedTag;
    untaggedTag["id"] = 1.0;
    untaggedTag["name"] = QStringLiteral("Untagged");
    untaggedTag["visible"] = true;
    QJsonObject wallsTag;
    wallsTag["id"] = toD(wallsTagId);
    wallsTag["name"] = QStringLiteral("Walls");
    wallsTag["visible"] = true;
    const QJsonArray expectedTags{untaggedTag, wallsTag};
    EXPECT_EQ(root.value("tags").toArray(), expectedTags);

    // kind string coverage: "instance" here, "edge" in hidden below, "vertex"
    // in the leader text's leaderTarget further down.
    const QJsonArray expectedAssignments{
        QJsonArray{QStringLiteral("instance"), toD(newInstanceId), toD(wallsTagId)}};
    EXPECT_EQ(root.value("tagAssignments").toArray(), expectedAssignments);

    const QJsonArray expectedHidden{QJsonArray{QStringLiteral("edge"), toD(wireEdgeId)}};
    EXPECT_EQ(root.value("hidden").toArray(), expectedHidden);

    // -- guides (sorted by id) ------------------------------------------------
    QJsonObject expectedLineGuide;
    expectedLineGuide["id"] = toD(lineGuideId);
    expectedLineGuide["isLine"] = true;
    expectedLineGuide["point"] = vecJson(0, 0, 5);
    expectedLineGuide["dir"] = vecJson(1, 0, 0);
    expectedLineGuide["hidden"] = false;
    QJsonObject expectedPointGuide;
    expectedPointGuide["id"] = toD(pointGuideId);
    expectedPointGuide["isLine"] = false;
    expectedPointGuide["point"] = vecJson(2, 2, 2);
    expectedPointGuide["dir"] = vecJson(0, 0, 0);
    expectedPointGuide["hidden"] = false;
    ASSERT_LT(lineGuideId, pointGuideId);
    const QJsonArray expectedGuides{expectedLineGuide, expectedPointGuide};
    EXPECT_EQ(root.value("guides").toArray(), expectedGuides);

    // -- dimensions -------------------------------------------------------
    QJsonObject expectedDim;
    expectedDim["id"] = toD(dimId);
    expectedDim["vertexA"] = toD(v00->id);
    expectedDim["vertexB"] = toD(v40->id);
    expectedDim["offsetDir"] = vecJson(0, 0, 1);
    expectedDim["offset"] = 0.5;
    expectedDim["overrideText"] = QString();
    expectedDim["associated"] = true;
    expectedDim["lastA"] = vecJson(0, 0, 0);
    expectedDim["lastB"] = vecJson(4, 0, 0);
    const QJsonArray expectedDimensions{expectedDim};
    EXPECT_EQ(root.value("dimensions").toArray(), expectedDimensions);

    // -- texts (sorted by id: screenText then leaderText) --------------------
    QJsonObject expectedScreenText;
    expectedScreenText["id"] = toD(screenTextId);
    expectedScreenText["screenFixed"] = true;
    expectedScreenText["screenX"] = 100.0;
    expectedScreenText["screenY"] = 200.0;
    expectedScreenText["worldAnchor"] = vecJson(0, 0, 0);
    expectedScreenText["leaderTarget"] = QJsonValue();
    expectedScreenText["text"] = QStringLiteral("Hello");

    QJsonObject expectedLeaderText;
    expectedLeaderText["id"] = toD(leaderTextId);
    expectedLeaderText["screenFixed"] = false;
    expectedLeaderText["screenX"] = 0.0;
    expectedLeaderText["screenY"] = 0.0;
    expectedLeaderText["worldAnchor"] = vecJson(1, 1, 0);
    expectedLeaderText["leaderTarget"] = QJsonArray{QStringLiteral("vertex"), toD(v00->id)};
    expectedLeaderText["text"] = QStringLiteral("Leader");

    ASSERT_LT(screenTextId, leaderTextId);
    const QJsonArray expectedTexts{expectedScreenText, expectedLeaderText};
    EXPECT_EQ(root.value("texts").toArray(), expectedTexts);

    // -- sectionPlanes ------------------------------------------------------
    QJsonObject expectedSection;
    expectedSection["id"] = toD(sectionId);
    expectedSection["name"] = QStringLiteral("Cut A");
    expectedSection["point"] = vecJson(0, 0, 1);
    expectedSection["normal"] = vecJson(0, 0, 1);
    expectedSection["active"] = true;
    expectedSection["hidden"] = false;
    const QJsonArray expectedSectionPlanes{expectedSection};
    EXPECT_EQ(root.value("sectionPlanes").toArray(), expectedSectionPlanes);

    // -- axes / camera --------------------------------------------------------
    QJsonObject expectedAxes;
    expectedAxes["origin"] = vecJson(1, 2, 3);
    expectedAxes["x"] = vecJson(0, 1, 0);
    expectedAxes["y"] = vecJson(1, 0, 0);
    expectedAxes["z"] = vecJson(0, 0, -1);
    EXPECT_EQ(root.value("axes").toObject(), expectedAxes);

    QJsonObject expectedCamera;
    expectedCamera["target"] = vecJson(5, 6, 7);
    expectedCamera["azimuthDeg"] = 45.0;
    expectedCamera["elevationDeg"] = 30.0;
    expectedCamera["distance"] = 12.5;
    expectedCamera["fovYDeg"] = 35.0;
    expectedCamera["projection"] = QStringLiteral("parallel");
    EXPECT_EQ(root.value("camera").toObject(), expectedCamera);

    // -- Whole-document equality: every piece above assembled into one
    // top-level object, compared against the writer's actual output in one
    // shot -- the strongest form of "assert the exact expected JSON".
    QJsonObject expectedRoot;
    expectedRoot["format"] = expectedFormat;
    expectedRoot["formatVersion"] = plnr::io::kFormatVersion;
    expectedRoot["meta"] = expectedMeta;
    expectedRoot["definitions"] = expectedDefinitions;
    expectedRoot["tags"] = expectedTags;
    expectedRoot["tagAssignments"] = expectedAssignments;
    expectedRoot["hidden"] = expectedHidden;
    expectedRoot["guides"] = expectedGuides;
    expectedRoot["dimensions"] = expectedDimensions;
    expectedRoot["texts"] = expectedTexts;
    expectedRoot["sectionPlanes"] = expectedSectionPlanes;
    expectedRoot["axes"] = expectedAxes;
    expectedRoot["camera"] = expectedCamera;
    expectedRoot["curves"] = QJsonValue();
    expectedRoot["materials"] = QJsonValue();

    EXPECT_EQ(root, expectedRoot);
    EXPECT_EQ(doc, QJsonDocument(expectedRoot));
}

TEST_F(PlrWriterTest, WritingTwiceProducesByteIdenticalJson) {
    const QJsonDocument doc1 =
        plnr::io::writeDocument(*geometry, *tags, *guides, *annotations, *sections, *axes, *materials, *style,
                                *shadow, *fog, camera, meta);
    const QJsonDocument doc2 =
        plnr::io::writeDocument(*geometry, *tags, *guides, *annotations, *sections, *axes, *materials, *style,
                                *shadow, *fog, camera, meta);

    const QByteArray bytes1 = doc1.toJson(QJsonDocument::Indented);
    const QByteArray bytes2 = doc2.toJson(QJsonDocument::Indented);

    EXPECT_FALSE(bytes1.isEmpty());
    EXPECT_EQ(bytes1, bytes2);
    EXPECT_EQ(doc1, doc2);
}

// Every events::Projection value writes its own pinned camera.projection
// string -- the fixture above only ever exercises Parallel, never TwoPoint.
// Minimal standalone agents (default-constructed) -- only CameraState varies.
TEST(PlrWriterCameraTest, EveryProjectionValueWritesItsOwnPinnedString) {
    const struct {
        plnr::events::Projection projection;
        const char* expected;
    } cases[] = {
        {plnr::events::Projection::Perspective, "perspective"},
        {plnr::events::Projection::Parallel, "parallel"},
        {plnr::events::Projection::TwoPoint, "twoPoint"},
    };
    for (const auto& c : cases) {
        const GeometryApi geometry;
        const TagStore tags;
        const GuideStore guides;
        const AnnotationStore annotations;
        const SectionStore sections;
        const AxesStore axes;
        const MaterialRepository materials;
        const StyleStore style;
        const ShadowStore shadow;
        const FogStore fog;
        CameraState camera{};
        camera.projection = c.projection;
        const DocumentMeta meta{QStringLiteral("test"), QStringLiteral("t"), QStringLiteral("in")};

        const QJsonObject root = plnr::io::writeDocument(geometry, tags, guides, annotations, sections, axes,
                                                          materials, style, shadow, fog, camera, meta)
                                      .object();
        EXPECT_EQ(root.value("camera").toObject().value("projection").toString(), QString::fromUtf8(c.expected))
            << "Projection enumerator -> \"" << c.expected << "\"";
    }
}

// Focused check with a NON-identity (translation) instance transform --
// the golden test's makeGroup placement is always identity, which can't
// distinguish a col/row mixup; add3dText places one via translation(origin), proving `t` lands in the LAST 3 slots.
TEST(PlrWriterTransformTest, InstanceTransformFlattensColumnsThenTranslation) {
    Kernel kernel;
    kernel.registerAgent(std::make_shared<GeometryApi>());
    auto geometry = kernel.agentAs<GeometryApi>(kGeometryApiName);

    const std::vector<std::vector<Vec3>> outlines{{Vec3{0, 0, 0}, Vec3{1, 0, 0}, Vec3{1, 1, 0}}};
    const Id instId = geometry->add3dText("Label", outlines, /*extrusion=*/0.0, Vec3{7, 8, 9});
    ASSERT_NE(instId, plnr::geo::kInvalidId);

    const TagStore tags;
    const GuideStore guides;
    const AnnotationStore annotations;
    const SectionStore sections;
    const AxesStore axes;
    const MaterialRepository materials;
    const StyleStore style;
    const ShadowStore shadow;
    const FogStore fog;
    const CameraState camera{};
    const DocumentMeta meta{QStringLiteral("test"), QStringLiteral("t"), QStringLiteral("in")};

    const QJsonDocument doc = plnr::io::writeDocument(*geometry, tags, guides, annotations, sections, axes, materials,
                                                       style, shadow, fog, camera, meta);
    const QJsonObject root = doc.object();
    const QJsonArray defs = root.value("definitions").toArray();
    ASSERT_EQ(defs.size(), 2);  // root + the new "3D Text" component definition

    const QJsonArray rootInstances = defs[0].toObject().value("instances").toArray();
    ASSERT_EQ(rootInstances.size(), 1);
    const QJsonArray transform = rootInstances[0].toObject().value("transform").toArray();
    const QJsonArray expectedTransform{1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0, 7.0, 8.0, 9.0};
    EXPECT_EQ(transform, expectedTransform);
}

// The compatibility contract plr_writer.h's own writeDocument comment
// documents: an empty MaterialRepository must write EXACTLY what every
// pre-M11 document wrote (`materials: null`, no `materialAssignments` key).
TEST(PlrWriterMaterialsTest, EmptyMaterialRepositoryWritesNullMaterialsAndOmitsAssignmentsKey) {
    const GeometryApi geometry;
    const TagStore tags;
    const GuideStore guides;
    const AnnotationStore annotations;
    const SectionStore sections;
    const AxesStore axes;
    const MaterialRepository materials;
    const StyleStore style;
    const ShadowStore shadow;
    const FogStore fog;
    const CameraState camera{};
    const DocumentMeta meta{QStringLiteral("test"), QStringLiteral("t"), QStringLiteral("in")};

    const QJsonObject root = plnr::io::writeDocument(geometry, tags, guides, annotations, sections, axes, materials,
                                                      style, shadow, fog, camera, meta)
                                  .object();

    EXPECT_TRUE(root.value("materials").isNull());
    EXPECT_FALSE(root.contains("materialAssignments"));
}

// Symmetric case: once a material exists, `materials` becomes the activated
// array and `materialAssignments` appears (even as `[]` when nothing has
// been painted yet).
TEST(PlrWriterMaterialsTest, NonEmptyMaterialRepositoryWritesArrayAndAssignmentsKey) {
    Kernel kernel;
    kernel.registerAgent(std::make_shared<MaterialRepository>());
    auto materials = kernel.agentAs<MaterialRepository>(kMaterialRepositoryName);
    const Id matId = materials->create("Wood", 0.5, 0.3, 0.1, 1.0);

    const GeometryApi geometry;
    const TagStore tags;
    const GuideStore guides;
    const AnnotationStore annotations;
    const SectionStore sections;
    const AxesStore axes;
    const StyleStore style;
    const ShadowStore shadow;
    const FogStore fog;
    const CameraState camera{};
    const DocumentMeta meta{QStringLiteral("test"), QStringLiteral("t"), QStringLiteral("in")};

    const QJsonObject root = plnr::io::writeDocument(geometry, tags, guides, annotations, sections, axes, *materials,
                                                      style, shadow, fog, camera, meta)
                                  .object();

    ASSERT_TRUE(root.value("materials").isArray());
    const QJsonArray materialsArr = root.value("materials").toArray();
    ASSERT_EQ(materialsArr.size(), 1);
    const QJsonObject matObj = materialsArr[0].toObject();
    EXPECT_EQ(matObj.value("id").toDouble(), toD(matId));
    EXPECT_EQ(matObj.value("name").toString(), QStringLiteral("Wood"));
    EXPECT_EQ(matObj.value("color").toArray(), (QJsonArray{0.5, 0.3, 0.1}));
    EXPECT_EQ(matObj.value("opacity").toDouble(), 1.0);
    EXPECT_TRUE(matObj.value("texture").isNull());
    EXPECT_TRUE(matObj.value("pbr").isNull());

    ASSERT_TRUE(root.contains("materialAssignments"));
    EXPECT_EQ(root.value("materialAssignments").toArray(), QJsonArray{});
}

// A textured material's `texture` field activates to {assetHash, tileW,
// tileH} instead of null. writeDocument never touches an AssetRepository, so
// this drives MaterialRepository::setTexture directly with an arbitrary hash.
TEST(PlrWriterMaterialsTest, TexturedMaterialWritesAssetHashTileWTileH) {
    Kernel kernel;
    kernel.registerAgent(std::make_shared<MaterialRepository>());
    auto materials = kernel.agentAs<MaterialRepository>(kMaterialRepositoryName);
    const Id matId = materials->create("Wood", 0.5, 0.3, 0.1, 1.0);
    ASSERT_TRUE(materials->setTexture(matId, "abc123def456abc1", 2.5, 3.5));

    const GeometryApi geometry;
    const TagStore tags;
    const GuideStore guides;
    const AnnotationStore annotations;
    const SectionStore sections;
    const AxesStore axes;
    const StyleStore style;
    const ShadowStore shadow;
    const FogStore fog;
    const CameraState camera{};
    const DocumentMeta meta{QStringLiteral("test"), QStringLiteral("t"), QStringLiteral("in")};

    const QJsonObject root = plnr::io::writeDocument(geometry, tags, guides, annotations, sections, axes, *materials,
                                                      style, shadow, fog, camera, meta)
                                  .object();

    const QJsonArray materialsArr = root.value("materials").toArray();
    ASSERT_EQ(materialsArr.size(), 1);
    const QJsonObject texture = materialsArr[0].toObject().value("texture").toObject();
    EXPECT_EQ(texture.value("assetHash").toString(), QStringLiteral("abc123def456abc1"));
    EXPECT_EQ(texture.value("tileW").toDouble(), 2.5);
    EXPECT_EQ(texture.value("tileH").toDouble(), 3.5);
}

// Same omit-when-default compatibility contract EmptyMaterialRepositoryWritesNullMaterialsAndOmitsAssignmentsKey
// proves for materials: an untouched StyleStore must produce NO `style`
// key at all (not even `null`), so every untouched document stays byte-identical.
TEST(PlrWriterStyleTest, AllDefaultStyleOmitsStyleKeyEntirely) {
    const GeometryApi geometry;
    const TagStore tags;
    const GuideStore guides;
    const AnnotationStore annotations;
    const SectionStore sections;
    const AxesStore axes;
    const MaterialRepository materials;
    const StyleStore style;
    const ShadowStore shadow;
    const FogStore fog;
    const CameraState camera{};
    const DocumentMeta meta{QStringLiteral("test"), QStringLiteral("t"), QStringLiteral("in")};

    const QJsonObject root = plnr::io::writeDocument(geometry, tags, guides, annotations, sections, axes, materials,
                                                      style, shadow, fog, camera, meta)
                                  .object();

    EXPECT_FALSE(root.contains("style"));
}

// Symmetric case: once ANY field differs from default (here, face style),
// the `style` object appears with every field populated -- including
// frontColor/backColor, which have no live setter yet, at their own defaults.
TEST(PlrWriterStyleTest, NonDefaultFaceStyleWritesFullStyleObject) {
    Kernel kernel;
    kernel.registerAgent(std::make_shared<StyleStore>());
    auto style = kernel.agentAs<StyleStore>(kStyleStoreName);
    ASSERT_TRUE(style->setFaceStyle(plnr::events::FaceStyle::XRay));

    const GeometryApi geometry;
    const TagStore tags;
    const GuideStore guides;
    const AnnotationStore annotations;
    const SectionStore sections;
    const AxesStore axes;
    const MaterialRepository materials;
    const ShadowStore shadow;
    const FogStore fog;
    const CameraState camera{};
    const DocumentMeta meta{QStringLiteral("test"), QStringLiteral("t"), QStringLiteral("in")};

    const QJsonObject root = plnr::io::writeDocument(geometry, tags, guides, annotations, sections, axes, materials,
                                                      *style, shadow, fog, camera, meta)
                                  .object();

    ASSERT_TRUE(root.contains("style"));
    const QJsonObject styleObj = root.value("style").toObject();
    EXPECT_EQ(styleObj.value("faceStyle").toString(), QStringLiteral("xray"));
    // Untouched flags/colors write their DEFAULTS -- profiles defaults ON.
    EXPECT_TRUE(styleObj.value("profiles").toBool());
    EXPECT_FALSE(styleObj.value("depthCue").toBool());
    EXPECT_FALSE(styleObj.value("backEdges").toBool());
    EXPECT_FALSE(styleObj.value("ambientOcclusion").toBool());
    EXPECT_EQ(styleObj.value("aoStrength").toDouble(), plnr::agent::kDefaultAoStrength);
    EXPECT_EQ(styleObj.value("frontColor").toArray(),
              (QJsonArray{plnr::agent::kDefaultFrontColorR, plnr::agent::kDefaultFrontColorG,
                          plnr::agent::kDefaultFrontColorB}));
    EXPECT_EQ(styleObj.value("backColor").toArray(),
              (QJsonArray{plnr::agent::kDefaultBackColorR, plnr::agent::kDefaultBackColorG,
                          plnr::agent::kDefaultBackColorB}));
}

// AO-only change (every other field stays at default) also triggers the
// `style` object -- isAllDefault() checks ambientOcclusion/aoStrength
// independently too, same as edge flags below.
TEST(PlrWriterStyleTest, NonDefaultAmbientOcclusionAloneWritesStyleObject) {
    Kernel kernel;
    kernel.registerAgent(std::make_shared<StyleStore>());
    auto style = kernel.agentAs<StyleStore>(kStyleStoreName);
    ASSERT_TRUE(style->setAmbientOcclusion(true));
    ASSERT_TRUE(style->setAoStrength(0.35));

    const GeometryApi geometry;
    const TagStore tags;
    const GuideStore guides;
    const AnnotationStore annotations;
    const SectionStore sections;
    const AxesStore axes;
    const MaterialRepository materials;
    const ShadowStore shadow;
    const FogStore fog;
    const CameraState camera{};
    const DocumentMeta meta{QStringLiteral("test"), QStringLiteral("t"), QStringLiteral("in")};

    const QJsonObject root = plnr::io::writeDocument(geometry, tags, guides, annotations, sections, axes, materials,
                                                      *style, shadow, fog, camera, meta)
                                  .object();

    ASSERT_TRUE(root.contains("style"));
    const QJsonObject styleObj = root.value("style").toObject();
    EXPECT_EQ(styleObj.value("faceStyle").toString(), QStringLiteral("shadedWithTextures"));
    EXPECT_TRUE(styleObj.value("ambientOcclusion").toBool());
    EXPECT_EQ(styleObj.value("aoStrength").toDouble(), 0.35);
}

// An edge-flag-only change (face style stays at its default) must ALSO
// trigger the `style` object -- isAllDefault() checks every field
// independently, not just faceStyle.
TEST(PlrWriterStyleTest, NonDefaultEdgeFlagAloneWritesStyleObject) {
    Kernel kernel;
    kernel.registerAgent(std::make_shared<StyleStore>());
    auto style = kernel.agentAs<StyleStore>(kStyleStoreName);
    ASSERT_TRUE(style->setEdgeFlag(plnr::events::EdgeFlag::DepthCue, true));

    const GeometryApi geometry;
    const TagStore tags;
    const GuideStore guides;
    const AnnotationStore annotations;
    const SectionStore sections;
    const AxesStore axes;
    const MaterialRepository materials;
    const ShadowStore shadow;
    const FogStore fog;
    const CameraState camera{};
    const DocumentMeta meta{QStringLiteral("test"), QStringLiteral("t"), QStringLiteral("in")};

    const QJsonObject root = plnr::io::writeDocument(geometry, tags, guides, annotations, sections, axes, materials,
                                                      *style, shadow, fog, camera, meta)
                                  .object();

    ASSERT_TRUE(root.contains("style"));
    const QJsonObject styleObj = root.value("style").toObject();
    EXPECT_EQ(styleObj.value("faceStyle").toString(), QStringLiteral("shadedWithTextures"));
    EXPECT_TRUE(styleObj.value("profiles").toBool());  // untouched -- default ON
    EXPECT_TRUE(styleObj.value("depthCue").toBool());
    EXPECT_FALSE(styleObj.value("backEdges").toBool());
}

// Pins the exact machine-readable string for every FaceStyle enumerator --
// same strings debug_bridge.cpp's cmdStyle uses, so a scenario script and
// this file agree on the wire format.
TEST(PlrWriterStyleTest, EveryFaceStyleValueWritesItsOwnPinnedString) {
    const struct {
        plnr::events::FaceStyle style;
        const char* expected;
    } cases[] = {
        {plnr::events::FaceStyle::Wireframe, "wireframe"},
        {plnr::events::FaceStyle::HiddenLine, "hiddenLine"},
        {plnr::events::FaceStyle::Shaded, "shaded"},
        {plnr::events::FaceStyle::ShadedWithTextures, "shadedWithTextures"},
        {plnr::events::FaceStyle::Monochrome, "monochrome"},
        {plnr::events::FaceStyle::XRay, "xray"},
    };
    for (const auto& c : cases) {
        Kernel kernel;
        kernel.registerAgent(std::make_shared<StyleStore>());
        auto style = kernel.agentAs<StyleStore>(kStyleStoreName);
        style->setFaceStyle(c.style);
        // A second, harmless field flip keeps ShadedWithTextures out of the
        // "all default" omission path so its own row is exercised too --
        // OFF, since profiles defaults ON (true would no-op).
        style->setEdgeFlag(plnr::events::EdgeFlag::Profiles, false);

        const GeometryApi geometry;
        const TagStore tags;
        const GuideStore guides;
        const AnnotationStore annotations;
        const SectionStore sections;
        const AxesStore axes;
        const MaterialRepository materials;
        const ShadowStore shadow;
        const FogStore fog;
        const CameraState camera{};
        const DocumentMeta meta{QStringLiteral("test"), QStringLiteral("t"), QStringLiteral("in")};

        const QJsonObject root = plnr::io::writeDocument(geometry, tags, guides, annotations, sections, axes,
                                                          materials, *style, shadow, fog, camera, meta)
                                      .object();
        EXPECT_EQ(root.value("style").toObject().value("faceStyle").toString(), QString::fromUtf8(c.expected))
            << "FaceStyle enumerator -> \"" << c.expected << "\"";
    }
}

// Same omit-when-default compatibility contract AllDefaultStyleOmitsStyleKeyEntirely
// proves for `style`: an untouched ShadowStore must produce NO `shadows`
// key at all (not even `null`), so an untouched document stays byte-identical.
TEST(PlrWriterShadowsTest, AllDefaultShadowsOmitsShadowsKeyEntirely) {
    const GeometryApi geometry;
    const TagStore tags;
    const GuideStore guides;
    const AnnotationStore annotations;
    const SectionStore sections;
    const AxesStore axes;
    const MaterialRepository materials;
    const StyleStore style;
    const ShadowStore shadow;
    const FogStore fog;
    const CameraState camera{};
    const DocumentMeta meta{QStringLiteral("test"), QStringLiteral("t"), QStringLiteral("in")};

    const QJsonObject root = plnr::io::writeDocument(geometry, tags, guides, annotations, sections, axes, materials,
                                                      style, shadow, fog, camera, meta)
                                  .object();

    EXPECT_FALSE(root.contains("shadows"));
}

// Symmetric case: once ANY field differs from default (here,
// `useSunForShading`), the `shadows` object appears with every field
// populated, including the rest at their own documented defaults.
TEST(PlrWriterShadowsTest, NonDefaultUseSunForShadingWritesFullShadowsObject) {
    Kernel kernel;
    kernel.registerAgent(std::make_shared<ShadowStore>());
    auto shadow = kernel.agentAs<ShadowStore>(kShadowStoreName);
    // OFF is the non-default value now that useSunForShading defaults ON
    // -- true would no-op and fail the ASSERT.
    ASSERT_TRUE(shadow->setUseSunForShading(false));

    const GeometryApi geometry;
    const TagStore tags;
    const GuideStore guides;
    const AnnotationStore annotations;
    const SectionStore sections;
    const AxesStore axes;
    const MaterialRepository materials;
    const StyleStore style;
    const FogStore fog;
    const CameraState camera{};
    const DocumentMeta meta{QStringLiteral("test"), QStringLiteral("t"), QStringLiteral("in")};

    const QJsonObject root = plnr::io::writeDocument(geometry, tags, guides, annotations, sections, axes, materials,
                                                      style, *shadow, fog, camera, meta)
                                  .object();

    ASSERT_TRUE(root.contains("shadows"));
    const QJsonObject shadowsObj = root.value("shadows").toObject();
    EXPECT_FALSE(shadowsObj.value("useSunForShading").toBool());
    EXPECT_FALSE(shadowsObj.value("showShadows").toBool());
    EXPECT_EQ(shadowsObj.value("latitudeDeg").toDouble(), plnr::agent::kDefaultLatitudeDeg);
    EXPECT_EQ(shadowsObj.value("longitudeDeg").toDouble(), plnr::agent::kDefaultLongitudeDeg);
    EXPECT_EQ(shadowsObj.value("month").toInt(), plnr::agent::kDefaultMonth);
    EXPECT_EQ(shadowsObj.value("day").toInt(), plnr::agent::kDefaultDay);
    EXPECT_EQ(shadowsObj.value("hourLocal").toDouble(), plnr::agent::kDefaultHourLocal);
    EXPECT_EQ(shadowsObj.value("light").toDouble(), plnr::agent::kDefaultLight);
    EXPECT_EQ(shadowsObj.value("dark").toDouble(), plnr::agent::kDefaultDark);
}

// setShowShadows alone (independent of useSunForShading) must ALSO
// trigger the `shadows` object.
TEST(PlrWriterShadowsTest, NonDefaultShowShadowsAloneWritesShadowsObject) {
    Kernel kernel;
    kernel.registerAgent(std::make_shared<ShadowStore>());
    auto shadow = kernel.agentAs<ShadowStore>(kShadowStoreName);
    ASSERT_TRUE(shadow->setShowShadows(true));

    const GeometryApi geometry;
    const TagStore tags;
    const GuideStore guides;
    const AnnotationStore annotations;
    const SectionStore sections;
    const AxesStore axes;
    const MaterialRepository materials;
    const StyleStore style;
    const FogStore fog;
    const CameraState camera{};
    const DocumentMeta meta{QStringLiteral("test"), QStringLiteral("t"), QStringLiteral("in")};

    const QJsonObject root = plnr::io::writeDocument(geometry, tags, guides, annotations, sections, axes, materials,
                                                      style, *shadow, fog, camera, meta)
                                  .object();

    ASSERT_TRUE(root.contains("shadows"));
    const QJsonObject shadowsObj = root.value("shadows").toObject();
    EXPECT_TRUE(shadowsObj.value("useSunForShading").toBool());  // untouched -- default ON
    EXPECT_TRUE(shadowsObj.value("showShadows").toBool());
}

// A position/date-time-only change (useSunForShading/showShadows stay at
// their defaults -- ON and OFF respectively) must ALSO trigger the
// `shadows` object -- isAllDefault() checks every field independently.
TEST(PlrWriterShadowsTest, NonDefaultPositionAloneWritesShadowsObject) {
    Kernel kernel;
    kernel.registerAgent(std::make_shared<ShadowStore>());
    auto shadow = kernel.agentAs<ShadowStore>(kShadowStoreName);
    ASSERT_TRUE(shadow->setPosition(51.48, -0.08));

    const GeometryApi geometry;
    const TagStore tags;
    const GuideStore guides;
    const AnnotationStore annotations;
    const SectionStore sections;
    const AxesStore axes;
    const MaterialRepository materials;
    const StyleStore style;
    const FogStore fog;
    const CameraState camera{};
    const DocumentMeta meta{QStringLiteral("test"), QStringLiteral("t"), QStringLiteral("in")};

    const QJsonObject root = plnr::io::writeDocument(geometry, tags, guides, annotations, sections, axes, materials,
                                                      style, *shadow, fog, camera, meta)
                                  .object();

    ASSERT_TRUE(root.contains("shadows"));
    const QJsonObject shadowsObj = root.value("shadows").toObject();
    EXPECT_TRUE(shadowsObj.value("useSunForShading").toBool());  // untouched -- default ON
    EXPECT_EQ(shadowsObj.value("latitudeDeg").toDouble(), 51.48);
    EXPECT_EQ(shadowsObj.value("longitudeDeg").toDouble(), -0.08);
}

// Same omit-when-default compatibility contract AllDefaultShadowsOmitsShadowsKeyEntirely
// proves for `shadows`: an untouched FogStore must produce NO `fog` key at all.
TEST(PlrWriterFogTest, AllDefaultFogOmitsFogKeyEntirely) {
    const GeometryApi geometry;
    const TagStore tags;
    const GuideStore guides;
    const AnnotationStore annotations;
    const SectionStore sections;
    const AxesStore axes;
    const MaterialRepository materials;
    const StyleStore style;
    const ShadowStore shadow;
    const FogStore fog;
    const CameraState camera{};
    const DocumentMeta meta{QStringLiteral("test"), QStringLiteral("t"), QStringLiteral("in")};

    const QJsonObject root = plnr::io::writeDocument(geometry, tags, guides, annotations, sections, axes, materials,
                                                      style, shadow, fog, camera, meta)
                                  .object();

    EXPECT_FALSE(root.contains("fog"));
}

// Symmetric case: once ANY field differs from default (here, `enabled`),
// the `fog` object appears with every field populated, including the
// rest at their own documented defaults.
TEST(PlrWriterFogTest, NonDefaultEnabledWritesFullFogObject) {
    Kernel kernel;
    kernel.registerAgent(std::make_shared<FogStore>());
    auto fog = kernel.agentAs<FogStore>(kFogStoreName);
    ASSERT_TRUE(fog->setEnabled(true));

    const GeometryApi geometry;
    const TagStore tags;
    const GuideStore guides;
    const AnnotationStore annotations;
    const SectionStore sections;
    const AxesStore axes;
    const MaterialRepository materials;
    const StyleStore style;
    const ShadowStore shadow;
    const CameraState camera{};
    const DocumentMeta meta{QStringLiteral("test"), QStringLiteral("t"), QStringLiteral("in")};

    const QJsonObject root = plnr::io::writeDocument(geometry, tags, guides, annotations, sections, axes, materials,
                                                      style, shadow, *fog, camera, meta)
                                  .object();

    ASSERT_TRUE(root.contains("fog"));
    const QJsonObject fogObj = root.value("fog").toObject();
    EXPECT_TRUE(fogObj.value("enabled").toBool());
    EXPECT_EQ(fogObj.value("startDistance").toDouble(), plnr::agent::kDefaultFogStartDistance);
    EXPECT_EQ(fogObj.value("endDistance").toDouble(), plnr::agent::kDefaultFogEndDistance);
    EXPECT_TRUE(fogObj.value("useBackgroundColor").toBool());
    EXPECT_EQ(fogObj.value("color").toArray(), (QJsonArray{0.5, 0.5, 0.5}));
}

// A range-only change (enabled stays at its default false) must ALSO
// trigger the `fog` object -- isAllDefault() checks every field
// independently, not just enabled.
TEST(PlrWriterFogTest, NonDefaultRangeAloneWritesFogObject) {
    Kernel kernel;
    kernel.registerAgent(std::make_shared<FogStore>());
    auto fog = kernel.agentAs<FogStore>(kFogStoreName);
    ASSERT_TRUE(fog->setRange(10.0, 80.0));

    const GeometryApi geometry;
    const TagStore tags;
    const GuideStore guides;
    const AnnotationStore annotations;
    const SectionStore sections;
    const AxesStore axes;
    const MaterialRepository materials;
    const StyleStore style;
    const ShadowStore shadow;
    const CameraState camera{};
    const DocumentMeta meta{QStringLiteral("test"), QStringLiteral("t"), QStringLiteral("in")};

    const QJsonObject root = plnr::io::writeDocument(geometry, tags, guides, annotations, sections, axes, materials,
                                                      style, shadow, *fog, camera, meta)
                                  .object();

    ASSERT_TRUE(root.contains("fog"));
    const QJsonObject fogObj = root.value("fog").toObject();
    EXPECT_FALSE(fogObj.value("enabled").toBool());
    EXPECT_EQ(fogObj.value("startDistance").toDouble(), 10.0);
    EXPECT_EQ(fogObj.value("endDistance").toDouble(), 80.0);
}

TEST(SniffContainerTest, ObjectWithNoLeadingWhitespaceIsRawJson) {
    EXPECT_EQ(plnr::io::sniffContainer(QByteArray("{\"a\":1}")), plnr::io::Container::RawJson);
}

TEST(SniffContainerTest, ObjectWithLeadingWhitespaceIsRawJson) {
    EXPECT_EQ(plnr::io::sniffContainer(QByteArray("  \t\r\n{\"a\":1}")), plnr::io::Container::RawJson);
}

TEST(SniffContainerTest, ZipMagicAtOffsetZeroIsZip) {
    const QByteArray head("PK\x03\x04", 4);
    EXPECT_EQ(plnr::io::sniffContainer(head), plnr::io::Container::Zip);
}

TEST(SniffContainerTest, ZipMagicWithLeadingByteIsUnknown) {
    // Zip is checked at offset 0 only -- a preceding byte, even whitespace,
    // disqualifies it (unlike the RawJson branch, which explicitly
    // tolerates leading whitespace).
    const QByteArray head(" PK\x03\x04", 5);
    EXPECT_EQ(plnr::io::sniffContainer(head), plnr::io::Container::Unknown);
}

TEST(SniffContainerTest, GarbageIsUnknown) {
    EXPECT_EQ(plnr::io::sniffContainer(QByteArray("not json or zip")), plnr::io::Container::Unknown);
}

TEST(SniffContainerTest, EmptyIsUnknown) {
    EXPECT_EQ(plnr::io::sniffContainer(QByteArray()), plnr::io::Container::Unknown);
}

TEST(SniffContainerTest, AllWhitespaceIsUnknown) {
    EXPECT_EQ(plnr::io::sniffContainer(QByteArray("   \t\n")), plnr::io::Container::Unknown);
}

}  // namespace
