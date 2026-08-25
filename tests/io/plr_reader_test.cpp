#include "io/plr_reader.h"

#include <algorithm>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QString>

#include <ordo/core/app_kernel.h>

#include "agent/annotation_store.h"
#include "agent/asset_repository.h"
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
#include "io/plr_container.h"
#include "io/plr_writer.h"

#include <geo/entity.h>
#include <geo/model.h>
#include <geo/scene.h>
#include <geo/vec3.h>

#include <gtest/gtest.h>

#include <miniz.h>

// Mirrors plr_writer_test.cpp's conventions (AppKernel-registered agents
// for mutating calls, plain-constructed agents where nothing sends an
// event). <miniz.h> is used only by the hand-forged-archive error test below.
namespace {

using ordo::core::AppKernel;
using plnr::agent::AnnotationStore;
using plnr::agent::AssetRepository;
using plnr::agent::AxesStore;
using plnr::agent::FogStore;
using plnr::agent::GeometryApi;
using plnr::agent::GuideStore;
using plnr::agent::kAnnotationStoreName;
using plnr::agent::kAssetRepositoryName;
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
using plnr::geo::EntityKind;
using plnr::geo::Id;
using plnr::io::assembleContainer;
using plnr::io::CameraState;
using plnr::io::DocumentMeta;
using plnr::io::ReadResult;

// GATE: representative doubles (including a 17-significant-digit value)
// round-tripped through QJsonDocument::toJson/fromJson must come back
// bit-identical -- see the maintainer notes.
TEST(PlrReaderPrecisionGateTest, DoublesRoundTripBitExactThroughQJsonDocument) {
    const std::vector<double> values = {
        0.1,
        1.0 / 3.0,
        1e-9,
        -12345.6789012345,
        1e17,
        1.2345678901234567,  // 17 significant digits
    };

    QJsonArray arr;
    for (double v : values) {
        arr.append(v);
    }
    QJsonObject obj;
    obj["values"] = arr;
    const QByteArray bytes = QJsonDocument(obj).toJson(QJsonDocument::Indented);

    QJsonParseError parseError;
    const QJsonDocument reparsed = QJsonDocument::fromJson(bytes, &parseError);
    ASSERT_EQ(parseError.error, QJsonParseError::NoError);
    const QJsonArray reparsedArr = reparsed.object().value("values").toArray();
    ASSERT_EQ(reparsedArr.size(), static_cast<int>(values.size()));

    for (int i = 0; i < reparsedArr.size(); ++i) {
        const double original = values[static_cast<std::size_t>(i)];
        const double roundTripped = reparsedArr[i].toDouble();
        EXPECT_EQ(std::memcmp(&original, &roundTripped, sizeof(double)), 0)
            << "value[" << i << "]: " << original << " -> " << roundTripped;
    }
}

// Same full-coverage document as plr_writer_test.cpp's PlrWriterTest fixture,
// reused here for a save -> load -> save byte-stability check.
class PlrRoundTripTest : public ::testing::Test {
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

        ASSERT_TRUE(geometry->addRectangle({0, 0, 0}, {4, 3, 0}));
        ASSERT_TRUE(geometry->addEdge({10, 0, 0}, {10, 5, 0}).created);
        const plnr::geo::Vertex* v00 = geometry->model().findVertex({0, 0, 0});
        ASSERT_NE(v00, nullptr);
        const plnr::geo::Vertex* v40 = geometry->model().findVertex({4, 0, 0});
        ASSERT_NE(v40, nullptr);

        Id rootFaceId = geometry->model().faces().begin()->first;

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

        wallsTagId = tags->createTag("Walls");
        ASSERT_TRUE(tags->assignTag({EntityRef{EntityKind::Instance, newInstanceId}}, wallsTagId));

        Id wireEdgeId = plnr::geo::kInvalidId;
        for (const auto& [id, e] : geometry->model().edges()) {
            (void)e;
            wireEdgeId = id;  // only the wire edge remains in root's model after makeGroup
        }
        ASSERT_NE(wireEdgeId, plnr::geo::kInvalidId);
        ASSERT_TRUE(geometry->setHidden({EntityRef{EntityKind::Edge, wireEdgeId}}, true));

        guides->addGuideLine({0, 0, 5}, {1, 0, 0});
        guides->addGuidePoint({2, 2, 2});

        annotations->addDimension(v00->id, v40->id, {0, 0, 1}, 0.5, geometry->model());
        annotations->addScreenText(100.0, 200.0, "Hello");
        annotations->addLeaderText({1, 1, 0}, EntityRef{EntityKind::Vertex, v00->id}, "Leader");

        const Id sectionId = sections->addPlane({0, 0, 1}, {0, 0, 1}, "Cut A");
        ASSERT_TRUE(sections->setActive(sectionId, true));

        ASSERT_TRUE(axes->set({1, 2, 3}, {0, 1, 0}, {1, 0, 0}));
    }

    AppKernel kernel;
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
    Id newInstanceId = 0;
    std::uint64_t wallsTagId = 0;

    // Projection set to a non-default value so this fixture's round-trip
    // test exercises the field's write+read path, not merely its default.
    CameraState camera{{5, 6, 7}, 45.0, 30.0, 12.5, 35.0, plnr::events::Projection::Parallel};
    DocumentMeta meta{QStringLiteral("test"), QStringLiteral("2026-08-22T00:00:00Z"), QStringLiteral("in")};
};

TEST_F(PlrRoundTripTest, WriteReadWriteProducesByteIdenticalJson) {
    const QByteArray bytesA =
        plnr::io::writeDocument(*geometry, *tags, *guides, *annotations, *sections, *axes, *materials, *style,
                                *shadow, *fog, camera, meta)
            .toJson(QJsonDocument::Indented);
    ASSERT_FALSE(bytesA.isEmpty());

    GeometryApi geometry2;
    TagStore tags2;
    GuideStore guides2;
    AnnotationStore annotations2;
    SectionStore sections2;
    AxesStore axes2;
    MaterialRepository materials2;
    AssetRepository assets2;
    StyleStore style2;
    ShadowStore shadow2;
    FogStore fog2;
    CameraState cameraOut;
    DocumentMeta metaOut;

    const ReadResult result = plnr::io::readDocument(bytesA, geometry2, tags2, guides2, annotations2, sections2, axes2,
                                                      materials2, assets2, style2, shadow2, fog2, &cameraOut, &metaOut);
    ASSERT_TRUE(result.ok) << result.error.toStdString();
    EXPECT_TRUE(result.error.isEmpty());

    const QByteArray bytesB =
        plnr::io::writeDocument(geometry2, tags2, guides2, annotations2, sections2, axes2, materials2, style2, shadow2,
                                fog2, cameraOut, metaOut)
            .toJson(QJsonDocument::Indented);
    EXPECT_EQ(bytesA, bytesB);

    EXPECT_EQ(cameraOut.target.x, camera.target.x);
    EXPECT_EQ(cameraOut.target.y, camera.target.y);
    EXPECT_EQ(cameraOut.target.z, camera.target.z);
    EXPECT_EQ(cameraOut.azimuthDeg, camera.azimuthDeg);
    EXPECT_EQ(cameraOut.elevationDeg, camera.elevationDeg);
    EXPECT_EQ(cameraOut.distance, camera.distance);
    EXPECT_EQ(cameraOut.fovYDeg, camera.fovYDeg);
    EXPECT_EQ(cameraOut.projection, camera.projection);

    EXPECT_EQ(metaOut.appVersion, meta.appVersion);
    EXPECT_EQ(metaOut.savedAt, meta.savedAt);
    EXPECT_EQ(metaOut.units, meta.units);
}

// Builds a staging Scene by hand (geo restore APIs) since GeometryApi's
// public API can't place a non-identity-linear-part instance. Pins
// column-major transform ordering: [col0, col1, col2, t].
TEST(PlrReaderRotationTransformTest, InstanceRotationTransformRoundTripsByteStableAndColumnMajor) {
    plnr::geo::Scene stagingScene;
    ASSERT_TRUE(stagingScene.restoreNextId(4));  // root=1, definition=2, instance=3
    plnr::geo::Definition* def = stagingScene.restoreDefinition(2, "RotatedThing", /*isGroup=*/true);
    ASSERT_NE(def, nullptr);
    ASSERT_TRUE(def->model.restoreNextId(8));  // vertices 1-3, edges 4-6, face 7
    ASSERT_TRUE(def->model.restoreVertex(1, plnr::geo::Vec3{0, 0, 0}));
    ASSERT_TRUE(def->model.restoreVertex(2, plnr::geo::Vec3{1, 0, 0}));
    ASSERT_TRUE(def->model.restoreVertex(3, plnr::geo::Vec3{0, 1, 0}));
    ASSERT_TRUE(def->model.restoreEdge(4, 1, 2));
    ASSERT_TRUE(def->model.restoreEdge(5, 2, 3));
    ASSERT_TRUE(def->model.restoreEdge(6, 3, 1));
    ASSERT_TRUE(def->model.restoreFace(7, std::vector<Id>{1, 2, 3}));

    const double angleRad = 30.0 * 3.14159265358979323846 / 180.0;
    const plnr::geo::Transform rotation =
        plnr::geo::Transform::rotation(plnr::geo::Vec3{0, 0, 0}, plnr::geo::Vec3{0, 0, 1}, angleRad);
    ASSERT_TRUE(stagingScene.restoreInstance(plnr::geo::kRootDefinitionId, 3, 2, rotation, "RotatedInstance"));
    // Non-identity linear part sanity check -- both trig terms present.
    ASSERT_NE(rotation.col0.y, 0.0);
    ASSERT_NE(rotation.col1.x, 0.0);

    GeometryApi geometry;
    geometry.adoptScene(std::move(stagingScene));

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

    const QJsonDocument docA = plnr::io::writeDocument(geometry, tags, guides, annotations, sections, axes, materials,
                                                        style, shadow, fog, camera, meta);
    const QByteArray bytesA = docA.toJson(QJsonDocument::Indented);

    const QJsonArray defsArr = docA.object().value("definitions").toArray();
    ASSERT_EQ(defsArr.size(), 2);
    const QJsonArray rootInstances = defsArr[0].toObject().value("instances").toArray();
    ASSERT_EQ(rootInstances.size(), 1);
    const QJsonArray transformArr = rootInstances[0].toObject().value("transform").toArray();
    const QJsonArray expectedTransform{
        rotation.col0.x, rotation.col0.y, rotation.col0.z, rotation.col1.x, rotation.col1.y, rotation.col1.z,
        rotation.col2.x, rotation.col2.y, rotation.col2.z, rotation.t.x,    rotation.t.y,    rotation.t.z,
    };
    EXPECT_EQ(transformArr, expectedTransform);

    GeometryApi geometry2;
    TagStore tags2;
    GuideStore guides2;
    AnnotationStore annotations2;
    SectionStore sections2;
    AxesStore axes2;
    MaterialRepository materials2;
    AssetRepository assets2;
    StyleStore style2;
    ShadowStore shadow2;
    FogStore fog2;
    CameraState cameraOut;
    DocumentMeta metaOut;
    const ReadResult result = plnr::io::readDocument(bytesA, geometry2, tags2, guides2, annotations2, sections2, axes2,
                                                      materials2, assets2, style2, shadow2, fog2, &cameraOut, &metaOut);
    ASSERT_TRUE(result.ok) << result.error.toStdString();

    const QByteArray bytesB =
        plnr::io::writeDocument(geometry2, tags2, guides2, annotations2, sections2, axes2, materials2, style2, shadow2,
                                fog2, cameraOut, metaOut)
            .toJson(QJsonDocument::Indented);
    EXPECT_EQ(bytesA, bytesB);
}

// -- Error-case coverage ---------------------------------------------------
// buildBaseDocument() produces a small valid document; each error test
// mutates one field, re-serializes, and asserts ok==false, a non-empty
// error, and untouched target agents (all-or-nothing).
QJsonObject buildBaseDocument() {
    AppKernel kernel;
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
    auto geometry = kernel.agentAs<GeometryApi>(kGeometryApiName);
    auto tags = kernel.agentAs<TagStore>(kTagStoreName);
    auto guides = kernel.agentAs<GuideStore>(kGuideStoreName);
    auto annotations = kernel.agentAs<AnnotationStore>(kAnnotationStoreName);
    auto sections = kernel.agentAs<SectionStore>(kSectionStoreName);
    auto axes = kernel.agentAs<AxesStore>(kAxesStoreName);
    auto materials = kernel.agentAs<MaterialRepository>(kMaterialRepositoryName);
    auto style = kernel.agentAs<StyleStore>(kStyleStoreName);
    auto shadow = kernel.agentAs<ShadowStore>(kShadowStoreName);
    auto fog = kernel.agentAs<FogStore>(kFogStoreName);

    geometry->addRectangle({0, 0, 0}, {4, 3, 0});
    const Id rootFaceId = geometry->model().faces().begin()->first;
    // makeGroup moves the rectangle's mesh into a new (non-root) definition
    // and places one instance of it back in root -- gives both a mesh
    // (definitions[1]) and an instance (definitions[0].instances[0]) to corrupt below.
    geometry->makeGroup({EntityRef{EntityKind::Face, rootFaceId}}, false, "Group1");

    const CameraState camera{{1, 2, 3}, 10.0, 20.0, 5.0, 40.0};
    const DocumentMeta meta{QStringLiteral("test"), QStringLiteral("t"), QStringLiteral("in")};
    return plnr::io::writeDocument(*geometry, *tags, *guides, *annotations, *sections, *axes, *materials, *style,
                                   *shadow, *fog, camera, meta)
        .object();
}

QByteArray toBytes(const QJsonObject& obj) {
    return QJsonDocument(obj).toJson(QJsonDocument::Indented);
}

// Nine agents pre-populated with an identifiable "marker" record each --
// must come out of a FAILED readDocument call completely unchanged.
struct MarkedStores {
    AppKernel kernel;
    std::shared_ptr<GeometryApi> geometry;
    std::shared_ptr<TagStore> tags;
    std::shared_ptr<GuideStore> guides;
    std::shared_ptr<AnnotationStore> annotations;
    std::shared_ptr<SectionStore> sections;
    std::shared_ptr<AxesStore> axes;
    std::shared_ptr<MaterialRepository> materials;
    std::shared_ptr<AssetRepository> assets;
    std::shared_ptr<StyleStore> style;
    std::shared_ptr<ShadowStore> shadow;
    std::shared_ptr<FogStore> fog;
};

MarkedStores buildMarkedStores() {
    MarkedStores m;
    m.kernel.registerAgent(std::make_shared<GeometryApi>());
    m.kernel.registerAgent(std::make_shared<TagStore>());
    m.kernel.registerAgent(std::make_shared<GuideStore>());
    m.kernel.registerAgent(std::make_shared<AnnotationStore>());
    m.kernel.registerAgent(std::make_shared<SectionStore>());
    m.kernel.registerAgent(std::make_shared<AxesStore>());
    m.kernel.registerAgent(std::make_shared<MaterialRepository>());
    m.kernel.registerAgent(std::make_shared<AssetRepository>());
    m.kernel.registerAgent(std::make_shared<StyleStore>());
    m.kernel.registerAgent(std::make_shared<ShadowStore>());
    m.kernel.registerAgent(std::make_shared<FogStore>());
    m.geometry = m.kernel.agentAs<GeometryApi>(kGeometryApiName);
    m.tags = m.kernel.agentAs<TagStore>(kTagStoreName);
    m.guides = m.kernel.agentAs<GuideStore>(kGuideStoreName);
    m.annotations = m.kernel.agentAs<AnnotationStore>(kAnnotationStoreName);
    m.sections = m.kernel.agentAs<SectionStore>(kSectionStoreName);
    m.axes = m.kernel.agentAs<AxesStore>(kAxesStoreName);
    m.materials = m.kernel.agentAs<MaterialRepository>(kMaterialRepositoryName);
    m.assets = m.kernel.agentAs<AssetRepository>(kAssetRepositoryName);
    m.style = m.kernel.agentAs<StyleStore>(kStyleStoreName);
    m.shadow = m.kernel.agentAs<ShadowStore>(kShadowStoreName);
    m.fog = m.kernel.agentAs<FogStore>(kFogStoreName);

    m.geometry->addRectangle({0, 0, 0}, {1, 1, 0});
    m.tags->createTag("PreexistingMarkerTag");
    m.guides->addGuidePoint({9, 9, 9});
    m.sections->addPlane({0, 0, 1}, {0, 0, 1}, "PreexistingPlane");
    EXPECT_TRUE(m.axes->set({1, 1, 1}, {0, 1, 0}, {1, 0, 0}));
    m.materials->create("PreexistingMarkerMaterial", 0.1, 0.2, 0.3, 1.0);
    m.assets->add("preexisting marker asset bytes", "png");
    // Marker style/shadow/fog values are non-default, so expectUntouched()
    // below can prove a rejected load leaves them alone too. OFF is the
    // non-default marker for ShadowStore (its default is ON).
    EXPECT_TRUE(m.style->setFaceStyle(plnr::events::FaceStyle::XRay));
    EXPECT_TRUE(m.shadow->setUseSunForShading(false));
    EXPECT_TRUE(m.fog->setEnabled(true));
    const plnr::geo::Vertex* v0 = m.geometry->model().findVertex({0, 0, 0});
    const plnr::geo::Vertex* v1 = m.geometry->model().findVertex({1, 0, 0});
    if (v0 != nullptr && v1 != nullptr) {
        m.annotations->addDimension(v0->id, v1->id, {0, 0, 1}, 0.1, m.geometry->model());
    }
    return m;
}

void expectUntouched(const MarkedStores& m) {
    EXPECT_EQ(m.geometry->model().faces().size(), 1u);
    ASSERT_EQ(m.tags->tags().size(), 2u);  // Untagged + PreexistingMarkerTag
    EXPECT_EQ(m.tags->tags()[1].name, "PreexistingMarkerTag");
    ASSERT_EQ(m.guides->guides().size(), 1u);
    EXPECT_EQ(m.guides->guides()[0].point.x, 9.0);
    ASSERT_EQ(m.sections->planes().size(), 1u);
    EXPECT_EQ(m.sections->planes()[0].name, "PreexistingPlane");
    EXPECT_EQ(m.axes->frame().origin.x, 1.0);
    EXPECT_EQ(m.annotations->dimensions().size(), 1u);
    ASSERT_EQ(m.materials->materials().size(), 1u);
    EXPECT_EQ(m.materials->materials()[0].name, "PreexistingMarkerMaterial");
    EXPECT_EQ(m.assets->hashes().size(), 1u);
    EXPECT_EQ(m.style->faceStyle(), plnr::events::FaceStyle::XRay);
    EXPECT_FALSE(m.shadow->useSunForShading());  // the non-default marker value (default is ON)
    EXPECT_TRUE(m.fog->enabled());
}

// Runs one error case end to end: bytes must fail to load and leave a fresh
// set of marked agents untouched.
void expectRejected(const QByteArray& bytes) {
    MarkedStores m = buildMarkedStores();
    CameraState cameraOut;
    DocumentMeta metaOut;
    const ReadResult result = plnr::io::readDocument(bytes, *m.geometry, *m.tags, *m.guides, *m.annotations,
                                                      *m.sections, *m.axes, *m.materials, *m.assets, *m.style,
                                                      *m.shadow, *m.fog, &cameraOut, &metaOut);
    EXPECT_FALSE(result.ok);
    EXPECT_FALSE(result.error.isEmpty());
    expectUntouched(m);
}

TEST(PlrReaderErrorTest, GarbageBytesAreRejected) {
    expectRejected(QByteArray("this is not json or zip at all"));
}

// A PK-sniffing but structurally-corrupt archive is io::openContainer's own
// "corrupt or unreadable ZIP archive" rejection, all-or-nothing like every
// other error path here.
TEST(PlrReaderErrorTest, CorruptZipBytesAreRejectedAllOrNothing) {
    MarkedStores m = buildMarkedStores();
    CameraState cameraOut;
    DocumentMeta metaOut;
    const QByteArray bytes("PK\x03\x04some zip-ish bytes that are not a real archive", 20);
    const ReadResult result = plnr::io::readDocument(bytes, *m.geometry, *m.tags, *m.guides, *m.annotations,
                                                      *m.sections, *m.axes, *m.materials, *m.assets, *m.style,
                                                      *m.shadow, *m.fog, &cameraOut, &metaOut);
    EXPECT_FALSE(result.ok);
    EXPECT_FALSE(result.error.isEmpty()) << result.error.toStdString();
    expectUntouched(m);
}

TEST(PlrReaderErrorTest, WrongFormatNameIsRejected) {
    QJsonObject doc = buildBaseDocument();
    doc["format"] = QStringLiteral("not-planura");
    expectRejected(toBytes(doc));
}

TEST(PlrReaderErrorTest, FormatVersionTooNewIsRejected) {
    QJsonObject doc = buildBaseDocument();
    doc["formatVersion"] = 999;
    expectRejected(toBytes(doc));
}

TEST(PlrReaderErrorTest, FaceLoopWithUnknownVertexIsRejected) {
    QJsonObject doc = buildBaseDocument();
    QJsonArray defs = doc.value("definitions").toArray();
    ASSERT_EQ(defs.size(), 2);
    QJsonObject groupDef = defs[1].toObject();  // definitions[1]: the group's own mesh
    QJsonObject mesh = groupDef.value("mesh").toObject();
    QJsonArray faces = mesh.value("faces").toArray();
    ASSERT_GE(faces.size(), 1);
    QJsonObject face0 = faces[0].toObject();
    QJsonArray loop = face0.value("loop").toArray();
    ASSERT_GE(loop.size(), 1);
    loop[0] = 999999.0;  // no vertex with this id exists in this mesh
    face0["loop"] = loop;
    faces[0] = face0;
    mesh["faces"] = faces;
    groupDef["mesh"] = mesh;
    defs[1] = groupDef;
    doc["definitions"] = defs;
    expectRejected(toBytes(doc));
}

TEST(PlrReaderErrorTest, FaceLoopWithMissingEdgeIsRejected) {
    QJsonObject doc = buildBaseDocument();
    QJsonArray defs = doc.value("definitions").toArray();
    ASSERT_EQ(defs.size(), 2);
    QJsonObject groupDef = defs[1].toObject();
    QJsonObject mesh = groupDef.value("mesh").toObject();
    QJsonArray edges = mesh.value("edges").toArray();
    ASSERT_GE(edges.size(), 1);
    edges.removeAt(0);  // breaks connectivity for whichever loop pair used it
    mesh["edges"] = edges;
    groupDef["mesh"] = mesh;
    defs[1] = groupDef;
    doc["definitions"] = defs;
    expectRejected(toBytes(doc));
}

TEST(PlrReaderErrorTest, InstanceWithUnknownDefinitionIdIsRejected) {
    QJsonObject doc = buildBaseDocument();
    QJsonArray defs = doc.value("definitions").toArray();
    ASSERT_EQ(defs.size(), 2);
    QJsonObject rootDef = defs[0].toObject();
    QJsonArray instances = rootDef.value("instances").toArray();
    ASSERT_EQ(instances.size(), 1);
    QJsonObject inst0 = instances[0].toObject();
    inst0["definitionId"] = 999999.0;
    instances[0] = inst0;
    rootDef["instances"] = instances;
    defs[0] = rootDef;
    doc["definitions"] = defs;
    expectRejected(toBytes(doc));
}

TEST(PlrReaderErrorTest, DuplicateVertexIdWithinMeshIsRejected) {
    QJsonObject doc = buildBaseDocument();
    QJsonArray defs = doc.value("definitions").toArray();
    ASSERT_EQ(defs.size(), 2);
    QJsonObject groupDef = defs[1].toObject();
    QJsonObject mesh = groupDef.value("mesh").toObject();
    QJsonArray vertices = mesh.value("vertices").toArray();
    ASSERT_GE(vertices.size(), 2);
    QJsonArray v0 = vertices[0].toArray();
    QJsonArray v1 = vertices[1].toArray();
    v1[0] = v0[0];  // duplicate the first vertex's id onto the second
    vertices[1] = v1;
    mesh["vertices"] = vertices;
    groupDef["mesh"] = mesh;
    defs[1] = groupDef;
    doc["definitions"] = defs;
    expectRejected(toBytes(doc));
}

TEST(PlrReaderErrorTest, DuplicateIdBetweenDefinitionAndInstanceIsRejected) {
    QJsonObject doc = buildBaseDocument();
    QJsonArray defs = doc.value("definitions").toArray();
    ASSERT_EQ(defs.size(), 2);
    const QJsonObject groupDef = defs[1].toObject();
    const double groupDefId = groupDef.value("id").toDouble();

    QJsonObject rootDef = defs[0].toObject();
    QJsonArray instances = rootDef.value("instances").toArray();
    ASSERT_EQ(instances.size(), 1);
    QJsonObject inst0 = instances[0].toObject();
    inst0["id"] = groupDefId;  // clash: this instance's own id == a definition's own id
    instances[0] = inst0;
    rootDef["instances"] = instances;
    defs[0] = rootDef;
    doc["definitions"] = defs;
    expectRejected(toBytes(doc));
}

// A file whose tagAssignments/hidden EntityRefs resolve to nothing must
// still load (verbatim acceptance is the documented contract) and
// round-trip byte-stable. Both stale refs are produced via the normal public API.
TEST(PlrReaderStaleRefTest, StaleHiddenAndTagAssignmentRefsLoadAndRoundTripByteStable) {
    AppKernel kernel;
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
    auto geometry = kernel.agentAs<GeometryApi>(kGeometryApiName);
    auto tags = kernel.agentAs<TagStore>(kTagStoreName);
    auto guides = kernel.agentAs<GuideStore>(kGuideStoreName);
    auto annotations = kernel.agentAs<AnnotationStore>(kAnnotationStoreName);
    auto sections = kernel.agentAs<SectionStore>(kSectionStoreName);
    auto axes = kernel.agentAs<AxesStore>(kAxesStoreName);
    auto materials = kernel.agentAs<MaterialRepository>(kMaterialRepositoryName);
    auto style = kernel.agentAs<StyleStore>(kStyleStoreName);
    auto shadow = kernel.agentAs<ShadowStore>(kShadowStoreName);
    auto fog = kernel.agentAs<FogStore>(kFogStoreName);

    // Stale tagAssignments entry: tag a wire edge, then delete it.
    ASSERT_TRUE(geometry->addEdge({0, 0, 0}, {1, 0, 0}).created);
    const Id danglingEdgeId = geometry->model().edges().begin()->first;
    const std::uint64_t staleTag = tags->createTag("Stale");
    ASSERT_TRUE(tags->assignTag({EntityRef{EntityKind::Edge, danglingEdgeId}}, staleTag));
    ASSERT_TRUE(geometry->removeEdge(danglingEdgeId));
    ASSERT_EQ(tags->assignments().size(), 1u);

    // Stale hidden entry: group a rectangle, hide its instance, explode it.
    ASSERT_TRUE(geometry->addRectangle({10, 10, 0}, {11, 11, 0}));
    const Id rectFaceId = geometry->model().faces().begin()->first;
    const Id groupInstanceId = geometry->makeGroup({EntityRef{EntityKind::Face, rectFaceId}}, false, "StaleGroup");
    ASSERT_NE(groupInstanceId, plnr::geo::kInvalidId);
    ASSERT_TRUE(geometry->setHidden({EntityRef{EntityKind::Instance, groupInstanceId}}, true));
    ASSERT_TRUE(geometry->explode(groupInstanceId));
    ASSERT_EQ(geometry->hidden().size(), 1u);

    const CameraState camera{};
    const DocumentMeta meta{QStringLiteral("test"), QStringLiteral("t"), QStringLiteral("in")};
    const QByteArray bytesA =
        plnr::io::writeDocument(*geometry, *tags, *guides, *annotations, *sections, *axes, *materials, *style,
                                *shadow, *fog, camera, meta)
            .toJson(QJsonDocument::Indented);

    GeometryApi geometry2;
    TagStore tags2;
    GuideStore guides2;
    AnnotationStore annotations2;
    SectionStore sections2;
    AxesStore axes2;
    MaterialRepository materials2;
    AssetRepository assets2;
    StyleStore style2;
    ShadowStore shadow2;
    FogStore fog2;
    CameraState cameraOut;
    DocumentMeta metaOut;
    const ReadResult result = plnr::io::readDocument(bytesA, geometry2, tags2, guides2, annotations2, sections2, axes2,
                                                      materials2, assets2, style2, shadow2, fog2, &cameraOut, &metaOut);
    ASSERT_TRUE(result.ok) << result.error.toStdString();

    EXPECT_EQ(tags2.assignments().size(), 1u);
    EXPECT_EQ(geometry2.hidden().size(), 1u);

    const QByteArray bytesB =
        plnr::io::writeDocument(geometry2, tags2, guides2, annotations2, sections2, axes2, materials2, style2, shadow2,
                                fog2, cameraOut, metaOut)
            .toJson(QJsonDocument::Indented);
    EXPECT_EQ(bytesA, bytesB);
}

// -- ZIP container branch --------------------------------------------------

// Happy path end to end: a textured material -> assembleContainer ->
// readDocument into fresh agents (material fields + asset bytes survive) -> re-save is byte-identical.
TEST(PlrReaderZipTest, TexturedMaterialRoundTripsThroughZipContainerAndReloadIsByteStable) {
    AppKernel kernel;
    kernel.registerAgent(std::make_shared<MaterialRepository>());
    kernel.registerAgent(std::make_shared<AssetRepository>());
    auto materials = kernel.agentAs<MaterialRepository>(kMaterialRepositoryName);
    auto assets = kernel.agentAs<AssetRepository>(kAssetRepositoryName);
    const std::string hash = assets->add("real texture bytes", "png");
    const Id matId = materials->create("Wood", 0.5, 0.3, 0.1, 1.0);
    ASSERT_TRUE(materials->setTexture(matId, hash, 2.0, 3.0));

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
    const QJsonDocument doc = plnr::io::writeDocument(geometry, tags, guides, annotations, sections, axes, *materials,
                                                       style, shadow, fog, camera, meta);
    const QByteArray zipBytesA = assembleContainer(doc, *assets);
    ASSERT_TRUE(zipBytesA.startsWith("PK"));

    GeometryApi geometry2;
    TagStore tags2;
    GuideStore guides2;
    AnnotationStore annotations2;
    SectionStore sections2;
    AxesStore axes2;
    MaterialRepository materials2;
    AssetRepository assets2;
    StyleStore style2;
    ShadowStore shadow2;
    FogStore fog2;
    CameraState cameraOut;
    DocumentMeta metaOut;
    const ReadResult result = plnr::io::readDocument(zipBytesA, geometry2, tags2, guides2, annotations2, sections2,
                                                      axes2, materials2, assets2, style2, shadow2, fog2, &cameraOut,
                                                      &metaOut);
    ASSERT_TRUE(result.ok) << result.error.toStdString();

    ASSERT_EQ(materials2.materials().size(), 1u);
    const Id reloadedId = materials2.materials()[0].id;
    ASSERT_NE(materials2.material(reloadedId), nullptr);
    EXPECT_EQ(materials2.material(reloadedId)->assetHash, hash);
    EXPECT_EQ(materials2.material(reloadedId)->tileW, 2.0);
    EXPECT_EQ(materials2.material(reloadedId)->tileH, 3.0);

    const auto* reloadedAsset = assets2.get(hash);
    ASSERT_NE(reloadedAsset, nullptr);
    EXPECT_EQ(reloadedAsset->bytes, "real texture bytes");
    EXPECT_EQ(reloadedAsset->ext, "png");

    const QJsonDocument doc2 = plnr::io::writeDocument(geometry2, tags2, guides2, annotations2, sections2, axes2,
                                                        materials2, style2, shadow2, fog2, cameraOut, metaOut);
    const QByteArray zipBytesB = assembleContainer(doc2, assets2);
    EXPECT_EQ(zipBytesA, zipBytesB);
}

// A plain non-ZIP save must still load through the same readDocument that
// also handles ZIP -- AssetRepository ends up empty, nothing else regresses.
TEST(PlrReaderZipTest, RawJsonDocumentStillLoadsWithEmptyAssetRepository) {
    const QByteArray bytes = toBytes(buildBaseDocument());
    ASSERT_FALSE(bytes.startsWith("PK"));

    GeometryApi geometry2;
    TagStore tags2;
    GuideStore guides2;
    AnnotationStore annotations2;
    SectionStore sections2;
    AxesStore axes2;
    MaterialRepository materials2;
    AssetRepository assets2;
    StyleStore style2;
    ShadowStore shadow2;
    FogStore fog2;
    CameraState cameraOut;
    DocumentMeta metaOut;
    const ReadResult result = plnr::io::readDocument(bytes, geometry2, tags2, guides2, annotations2, sections2, axes2,
                                                      materials2, assets2, style2, shadow2, fog2, &cameraOut, &metaOut);
    ASSERT_TRUE(result.ok) << result.error.toStdString();
    EXPECT_TRUE(assets2.hashes().empty());
}

// A material's assetHash naming no ZIP asset entry rejects the whole file.
// assembleContainer is given an AssetRepository that deliberately lacks the hash.
TEST(PlrReaderZipErrorTest, DanglingZipAssetReferenceRejectsAllOrNothing) {
    AppKernel kernel;
    kernel.registerAgent(std::make_shared<MaterialRepository>());
    auto materials = kernel.agentAs<MaterialRepository>(kMaterialRepositoryName);
    AssetRepository sourceAssets;  // NOT the agent handed to assembleContainer below
    const std::string hash = sourceAssets.add("real texture bytes", "png");
    const Id matId = materials->create("Wood", 0.5, 0.3, 0.1, 1.0);
    ASSERT_TRUE(materials->setTexture(matId, hash, 1.0, 1.0));

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
    const QJsonDocument doc = plnr::io::writeDocument(geometry, tags, guides, annotations, sections, axes, *materials,
                                                       style, shadow, fog, camera, meta);

    AssetRepository emptyAssets;  // deliberately does NOT contain `hash`
    const QByteArray zipBytes = assembleContainer(doc, emptyAssets);
    ASSERT_TRUE(zipBytes.startsWith("PK"));

    expectRejected(zipBytes);
}

// A ZIP asset entry whose content doesn't hash to its claimed filename also
// rejects the whole file -- hand-forged via miniz (assembleContainer's own gate can't produce this).
TEST(PlrReaderZipErrorTest, AssetContentHashMismatchRejectsAllOrNothing) {
    QJsonObject doc = buildBaseDocument();
    QJsonObject material;
    material["id"] = 1.0;
    material["name"] = QStringLiteral("Forged");
    material["color"] = QJsonArray{0.5, 0.5, 0.5};
    material["opacity"] = 1.0;
    QJsonObject texture;
    texture["assetHash"] = QStringLiteral("deadbeefcafef00d");  // 16 hex chars -- never actually computed from bytes below
    texture["tileW"] = 1.0;
    texture["tileH"] = 1.0;
    material["texture"] = texture;
    material["pbr"] = QJsonValue();
    doc["materials"] = QJsonArray{material};
    doc["materialAssignments"] = QJsonArray{};
    const QByteArray modelJsonBytes = toBytes(doc);

    mz_zip_archive zip;
    mz_zip_zero_struct(&zip);
    ASSERT_TRUE(mz_zip_writer_init_heap(&zip, 0, 4096));
    ASSERT_TRUE(mz_zip_writer_add_mem(&zip, "model.json", modelJsonBytes.constData(),
                                       static_cast<size_t>(modelJsonBytes.size()), MZ_DEFAULT_COMPRESSION));
    const QByteArray forgedAssetBytes("this content does not hash to deadbeefcafef00d");
    ASSERT_TRUE(mz_zip_writer_add_mem(&zip, "assets/deadbeefcafef00d.png", forgedAssetBytes.constData(),
                                       static_cast<size_t>(forgedAssetBytes.size()), MZ_NO_COMPRESSION));
    void* heapBuf = nullptr;
    size_t heapSize = 0;
    ASSERT_TRUE(mz_zip_writer_finalize_heap_archive(&zip, &heapBuf, &heapSize));
    const QByteArray zipBytes(static_cast<const char*>(heapBuf), static_cast<int>(heapSize));
    mz_free(heapBuf);
    mz_zip_writer_end(&zip);
    ASSERT_TRUE(zipBytes.startsWith("PK"));

    expectRejected(zipBytes);
}

// -- Style round-trip -------------------------------------------------------

// A document with no `style` key must load and leave StyleStore at its
// ctor-seeded default -- the reader's half of the compatibility contract.
TEST(PlrReaderStyleTest, AbsentStyleKeyLeavesStoreAtDefault) {
    const QByteArray bytes = toBytes(buildBaseDocument());
    ASSERT_FALSE(bytes.contains("\"style\""));

    GeometryApi geometry2;
    TagStore tags2;
    GuideStore guides2;
    AnnotationStore annotations2;
    SectionStore sections2;
    AxesStore axes2;
    MaterialRepository materials2;
    AssetRepository assets2;
    StyleStore style2;
    ShadowStore shadow2;
    FogStore fog2;
    CameraState cameraOut;
    DocumentMeta metaOut;
    const ReadResult result = plnr::io::readDocument(bytes, geometry2, tags2, guides2, annotations2, sections2, axes2,
                                                      materials2, assets2, style2, shadow2, fog2, &cameraOut, &metaOut);
    ASSERT_TRUE(result.ok) << result.error.toStdString();
    EXPECT_TRUE(style2.isAllDefault());
    EXPECT_EQ(style2.faceStyle(), plnr::events::FaceStyle::ShadedWithTextures);
}

// A non-default style (every field touched) round-trips write -> read -> write byte-stable.
TEST(PlrReaderStyleTest, NonDefaultStyleRoundTripsByteStable) {
    AppKernel kernel;
    kernel.registerAgent(std::make_shared<StyleStore>());
    auto style = kernel.agentAs<StyleStore>(kStyleStoreName);
    ASSERT_TRUE(style->setFaceStyle(plnr::events::FaceStyle::HiddenLine));
    // OFF is the non-default value now that profiles defaults ON --
    // a true here would no-op and fail the ASSERT.
    ASSERT_TRUE(style->setEdgeFlag(plnr::events::EdgeFlag::Profiles, false));
    ASSERT_TRUE(style->setEdgeFlag(plnr::events::EdgeFlag::BackEdges, true));
    ASSERT_TRUE(style->setAmbientOcclusion(true));
    ASSERT_TRUE(style->setAoStrength(0.42));

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
    const QByteArray bytesA = plnr::io::writeDocument(geometry, tags, guides, annotations, sections, axes, materials,
                                                       *style, shadow, fog, camera, meta)
                                   .toJson(QJsonDocument::Indented);
    ASSERT_TRUE(bytesA.contains("\"style\""));

    GeometryApi geometry2;
    TagStore tags2;
    GuideStore guides2;
    AnnotationStore annotations2;
    SectionStore sections2;
    AxesStore axes2;
    MaterialRepository materials2;
    AssetRepository assets2;
    StyleStore style2;
    ShadowStore shadow2;
    FogStore fog2;
    CameraState cameraOut;
    DocumentMeta metaOut;
    const ReadResult result = plnr::io::readDocument(bytesA, geometry2, tags2, guides2, annotations2, sections2, axes2,
                                                      materials2, assets2, style2, shadow2, fog2, &cameraOut, &metaOut);
    ASSERT_TRUE(result.ok) << result.error.toStdString();

    EXPECT_EQ(style2.faceStyle(), plnr::events::FaceStyle::HiddenLine);
    EXPECT_FALSE(style2.profiles());
    EXPECT_FALSE(style2.depthCue());
    EXPECT_TRUE(style2.backEdges());
    EXPECT_TRUE(style2.ambientOcclusion());
    EXPECT_EQ(style2.aoStrength(), 0.42);

    const QByteArray bytesB = plnr::io::writeDocument(geometry2, tags2, guides2, annotations2, sections2, axes2,
                                                       materials2, style2, shadow2, fog2, cameraOut, metaOut)
                                   .toJson(QJsonDocument::Indented);
    EXPECT_EQ(bytesA, bytesB);
}

// -- Style error cases ------------------------------------------------------

// An unrecognized `style.faceStyle` string (typo, or a future value this
// reader doesn't know about yet) must reject the WHOLE file, all-or-nothing
// -- same discipline as every other enum/reference check here.
TEST(PlrReaderErrorTest, UnknownFaceStyleStringIsRejected) {
    QJsonObject doc = buildBaseDocument();
    QJsonObject styleObj;
    styleObj["faceStyle"] = QStringLiteral("not-a-real-style");
    styleObj["profiles"] = false;
    styleObj["depthCue"] = false;
    styleObj["backEdges"] = false;
    styleObj["frontColor"] = QJsonArray{1.0, 1.0, 1.0};
    styleObj["backColor"] = QJsonArray{0.5, 0.5, 0.5};
    doc["style"] = styleObj;
    expectRejected(toBytes(doc));
}

// A `style` object missing a required field (here, `backEdges`) is
// malformed, not merely "some fields at default" -- same "every field
// required, no partial object" discipline plr_reader.cpp's own parseStyle comment follows.
TEST(PlrReaderErrorTest, StyleObjectMissingRequiredFieldIsRejected) {
    QJsonObject doc = buildBaseDocument();
    QJsonObject styleObj;
    styleObj["faceStyle"] = QStringLiteral("wireframe");
    styleObj["profiles"] = false;
    styleObj["depthCue"] = false;
    // backEdges deliberately omitted
    styleObj["frontColor"] = QJsonArray{1.0, 1.0, 1.0};
    styleObj["backColor"] = QJsonArray{0.5, 0.5, 0.5};
    doc["style"] = styleObj;
    expectRejected(toBytes(doc));
}

// A `style` object missing ambientOcclusion/aoStrength is rejected the same
// way -- same "every field required, no partial object" discipline
// StyleObjectMissingRequiredFieldIsRejected above already proves for backEdges.
TEST(PlrReaderErrorTest, StyleObjectMissingAmbientOcclusionFieldIsRejected) {
    QJsonObject doc = buildBaseDocument();
    QJsonObject styleObj;
    styleObj["faceStyle"] = QStringLiteral("wireframe");
    styleObj["profiles"] = false;
    styleObj["depthCue"] = false;
    styleObj["backEdges"] = false;
    styleObj["frontColor"] = QJsonArray{1.0, 1.0, 1.0};
    styleObj["backColor"] = QJsonArray{0.5, 0.5, 0.5};
    // ambientOcclusion/aoStrength deliberately omitted
    doc["style"] = styleObj;
    expectRejected(toBytes(doc));
}

// -- Shadows round-trip ------------------------------------------------------

// A document with no `shadows` key must load and leave ShadowStore at its ctor-seeded default.
TEST(PlrReaderShadowsTest, AbsentShadowsKeyLeavesStoreAtDefault) {
    const QByteArray bytes = toBytes(buildBaseDocument());
    ASSERT_FALSE(bytes.contains("\"shadows\""));

    GeometryApi geometry2;
    TagStore tags2;
    GuideStore guides2;
    AnnotationStore annotations2;
    SectionStore sections2;
    AxesStore axes2;
    MaterialRepository materials2;
    AssetRepository assets2;
    StyleStore style2;
    ShadowStore shadow2;
    FogStore fog2;
    CameraState cameraOut;
    DocumentMeta metaOut;
    const ReadResult result = plnr::io::readDocument(bytes, geometry2, tags2, guides2, annotations2, sections2, axes2,
                                                      materials2, assets2, style2, shadow2, fog2, &cameraOut, &metaOut);
    ASSERT_TRUE(result.ok) << result.error.toStdString();
    EXPECT_TRUE(shadow2.isAllDefault());
    EXPECT_TRUE(shadow2.useSunForShading());  // default is ON 
    EXPECT_FALSE(shadow2.showShadows());
}

// A non-default shadows state (every field touched) round-trips write ->
// read -> write byte-stable, same pattern
// PlrReaderStyleTest.NonDefaultStyleRoundTripsByteStable already uses.
TEST(PlrReaderShadowsTest, NonDefaultShadowsRoundTripsByteStable) {
    AppKernel kernel;
    kernel.registerAgent(std::make_shared<ShadowStore>());
    auto shadow = kernel.agentAs<ShadowStore>(kShadowStoreName);
    // OFF is the non-default value now -- true would no-op and fail the ASSERT.
    ASSERT_TRUE(shadow->setUseSunForShading(false));
    ASSERT_TRUE(shadow->setShowShadows(true));
    ASSERT_TRUE(shadow->setPosition(51.48, -0.08));
    ASSERT_TRUE(shadow->setDateTime(6, 21, 9.0));
    ASSERT_TRUE(shadow->setLight(60.0));
    ASSERT_TRUE(shadow->setDark(30.0));

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
    const QByteArray bytesA = plnr::io::writeDocument(geometry, tags, guides, annotations, sections, axes, materials,
                                                       style, *shadow, fog, camera, meta)
                                   .toJson(QJsonDocument::Indented);
    ASSERT_TRUE(bytesA.contains("\"shadows\""));

    GeometryApi geometry2;
    TagStore tags2;
    GuideStore guides2;
    AnnotationStore annotations2;
    SectionStore sections2;
    AxesStore axes2;
    MaterialRepository materials2;
    AssetRepository assets2;
    StyleStore style2;
    ShadowStore shadow2;
    FogStore fog2;
    CameraState cameraOut;
    DocumentMeta metaOut;
    const ReadResult result = plnr::io::readDocument(bytesA, geometry2, tags2, guides2, annotations2, sections2, axes2,
                                                      materials2, assets2, style2, shadow2, fog2, &cameraOut, &metaOut);
    ASSERT_TRUE(result.ok) << result.error.toStdString();

    EXPECT_FALSE(shadow2.useSunForShading());
    EXPECT_TRUE(shadow2.showShadows());
    EXPECT_EQ(shadow2.latitudeDeg(), 51.48);
    EXPECT_EQ(shadow2.longitudeDeg(), -0.08);
    EXPECT_EQ(shadow2.month(), 6);
    EXPECT_EQ(shadow2.day(), 21);
    EXPECT_EQ(shadow2.hourLocal(), 9.0);
    EXPECT_EQ(shadow2.light(), 60.0);
    EXPECT_EQ(shadow2.dark(), 30.0);

    const QByteArray bytesB = plnr::io::writeDocument(geometry2, tags2, guides2, annotations2, sections2, axes2,
                                                       materials2, style2, shadow2, fog2, cameraOut, metaOut)
                                   .toJson(QJsonDocument::Indented);
    EXPECT_EQ(bytesA, bytesB);
}

// -- Shadows error cases -----------------------------------------------------

// A `shadows` object missing a required field (here, `dark`) is malformed,
// not merely "some fields at default" -- same discipline
// PlrReaderErrorTest.StyleObjectMissingRequiredFieldIsRejected already establishes.
TEST(PlrReaderErrorTest, ShadowsObjectMissingRequiredFieldIsRejected) {
    QJsonObject doc = buildBaseDocument();
    QJsonObject shadowsObj;
    shadowsObj["useSunForShading"] = true;
    shadowsObj["showShadows"] = false;
    shadowsObj["latitudeDeg"] = plnr::agent::kDefaultLatitudeDeg;
    shadowsObj["longitudeDeg"] = plnr::agent::kDefaultLongitudeDeg;
    shadowsObj["month"] = plnr::agent::kDefaultMonth;
    shadowsObj["day"] = plnr::agent::kDefaultDay;
    shadowsObj["hourLocal"] = plnr::agent::kDefaultHourLocal;
    shadowsObj["light"] = plnr::agent::kDefaultLight;
    // dark deliberately omitted
    doc["shadows"] = shadowsObj;
    expectRejected(toBytes(doc));
}

// The old field name `enabled` is no longer recognized -- a file carrying
// it is missing the now-required `useSunForShading` field and is rejected
// (no back-compat parsing was added: the old name never shipped beyond this repo's own test fixtures).
TEST(PlrReaderErrorTest, PreRestructureEnabledFieldNameIsNotRecognized) {
    QJsonObject doc = buildBaseDocument();
    QJsonObject shadowsObj;
    shadowsObj["enabled"] = true;  // old step 1/3 field name -- unrecognized as of step 2/3
    shadowsObj["latitudeDeg"] = plnr::agent::kDefaultLatitudeDeg;
    shadowsObj["longitudeDeg"] = plnr::agent::kDefaultLongitudeDeg;
    shadowsObj["month"] = plnr::agent::kDefaultMonth;
    shadowsObj["day"] = plnr::agent::kDefaultDay;
    shadowsObj["hourLocal"] = plnr::agent::kDefaultHourLocal;
    doc["shadows"] = shadowsObj;
    expectRejected(toBytes(doc));
}

// -- Fog round-trip -----------------------------------------------------------

// A document with no `fog` key at all must load successfully and leave the
// target FogStore at its ctor-seeded default -- mirrors
// PlrReaderShadowsTest.AbsentShadowsKeyLeavesStoreAtDefault.
TEST(PlrReaderFogTest, AbsentFogKeyLeavesStoreAtDefault) {
    const QByteArray bytes = toBytes(buildBaseDocument());
    ASSERT_FALSE(bytes.contains("\"fog\""));

    GeometryApi geometry2;
    TagStore tags2;
    GuideStore guides2;
    AnnotationStore annotations2;
    SectionStore sections2;
    AxesStore axes2;
    MaterialRepository materials2;
    AssetRepository assets2;
    StyleStore style2;
    ShadowStore shadow2;
    FogStore fog2;
    CameraState cameraOut;
    DocumentMeta metaOut;
    const ReadResult result = plnr::io::readDocument(bytes, geometry2, tags2, guides2, annotations2, sections2, axes2,
                                                      materials2, assets2, style2, shadow2, fog2, &cameraOut, &metaOut);
    ASSERT_TRUE(result.ok) << result.error.toStdString();
    EXPECT_TRUE(fog2.isAllDefault());
    EXPECT_FALSE(fog2.enabled());
}

// A non-default fog state (every field touched) round-trips write -> read ->
// write byte-stable.
TEST(PlrReaderFogTest, NonDefaultFogRoundTripsByteStable) {
    AppKernel kernel;
    kernel.registerAgent(std::make_shared<FogStore>());
    auto fog = kernel.agentAs<FogStore>(kFogStoreName);
    ASSERT_TRUE(fog->setEnabled(true));
    ASSERT_TRUE(fog->setRange(10.0, 80.0));
    ASSERT_TRUE(fog->setUseBackgroundColor(false));

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
    const QByteArray bytesA = plnr::io::writeDocument(geometry, tags, guides, annotations, sections, axes, materials,
                                                       style, shadow, *fog, camera, meta)
                                   .toJson(QJsonDocument::Indented);
    ASSERT_TRUE(bytesA.contains("\"fog\""));

    GeometryApi geometry2;
    TagStore tags2;
    GuideStore guides2;
    AnnotationStore annotations2;
    SectionStore sections2;
    AxesStore axes2;
    MaterialRepository materials2;
    AssetRepository assets2;
    StyleStore style2;
    ShadowStore shadow2;
    FogStore fog2;
    CameraState cameraOut;
    DocumentMeta metaOut;
    const ReadResult result = plnr::io::readDocument(bytesA, geometry2, tags2, guides2, annotations2, sections2, axes2,
                                                      materials2, assets2, style2, shadow2, fog2, &cameraOut, &metaOut);
    ASSERT_TRUE(result.ok) << result.error.toStdString();

    EXPECT_TRUE(fog2.enabled());
    EXPECT_EQ(fog2.startDistance(), 10.0);
    EXPECT_EQ(fog2.endDistance(), 80.0);
    EXPECT_FALSE(fog2.useBackgroundColor());

    const QByteArray bytesB = plnr::io::writeDocument(geometry2, tags2, guides2, annotations2, sections2, axes2,
                                                       materials2, style2, shadow2, fog2, cameraOut, metaOut)
                                   .toJson(QJsonDocument::Indented);
    EXPECT_EQ(bytesA, bytesB);
}

// -- Fog error cases ----------------------------------------------------------

// A `fog` object missing a required field (here, `color`) is malformed --
// same "every field required, no partial object" discipline every other
// object-shaped section here follows.
TEST(PlrReaderErrorTest, FogObjectMissingRequiredFieldIsRejected) {
    QJsonObject doc = buildBaseDocument();
    QJsonObject fogObj;
    fogObj["enabled"] = true;
    fogObj["startDistance"] = plnr::agent::kDefaultFogStartDistance;
    fogObj["endDistance"] = plnr::agent::kDefaultFogEndDistance;
    fogObj["useBackgroundColor"] = true;
    // color deliberately omitted
    doc["fog"] = fogObj;
    expectRejected(toBytes(doc));
}

// -- Camera projection compat ------------------

// A `camera` object with no `projection` key must still load, leaving
// cameraOut at the Perspective default -- the camera block itself is
// required, only this one field is optional.
TEST(PlrReaderCameraTest, AbsentProjectionFieldDefaultsToPerspective) {
    QJsonObject doc = buildBaseDocument();
    QJsonObject cameraObj = doc.value("camera").toObject();
    ASSERT_TRUE(cameraObj.contains(QStringLiteral("projection")));
    cameraObj.remove(QStringLiteral("projection"));
    doc["camera"] = cameraObj;
    const QByteArray bytes = toBytes(doc);
    ASSERT_FALSE(bytes.contains("\"projection\""));

    GeometryApi geometry2;
    TagStore tags2;
    GuideStore guides2;
    AnnotationStore annotations2;
    SectionStore sections2;
    AxesStore axes2;
    MaterialRepository materials2;
    AssetRepository assets2;
    StyleStore style2;
    ShadowStore shadow2;
    FogStore fog2;
    CameraState cameraOut;
    DocumentMeta metaOut;
    const ReadResult result = plnr::io::readDocument(bytes, geometry2, tags2, guides2, annotations2, sections2, axes2,
                                                      materials2, assets2, style2, shadow2, fog2, &cameraOut, &metaOut);
    ASSERT_TRUE(result.ok) << result.error.toStdString();
    EXPECT_EQ(cameraOut.projection, plnr::events::Projection::Perspective);
}

// An unrecognized `camera.projection` string must reject the WHOLE file,
// all-or-nothing -- same discipline as
// PlrReaderErrorTest.UnknownFaceStyleStringIsRejected; only ABSENCE is a tolerated compatibility case, not corruption.
TEST(PlrReaderErrorTest, UnknownProjectionStringIsRejected) {
    QJsonObject doc = buildBaseDocument();
    QJsonObject cameraObj = doc.value("camera").toObject();
    cameraObj["projection"] = QStringLiteral("not-a-real-projection");
    doc["camera"] = cameraObj;
    expectRejected(toBytes(doc));
}

// The third projection value -- confirms projectionFromString's "twoPoint"
// branch is actually reachable/correct, not just that the two pre-existing
// values still parse (PlrRoundTripTest above only exercises Perspective/Parallel).
TEST(PlrReaderCameraTest, TwoPointProjectionStringParses) {
    QJsonObject doc = buildBaseDocument();
    QJsonObject cameraObj = doc.value("camera").toObject();
    cameraObj["projection"] = QStringLiteral("twoPoint");
    doc["camera"] = cameraObj;
    const QByteArray bytes = toBytes(doc);

    GeometryApi geometry2;
    TagStore tags2;
    GuideStore guides2;
    AnnotationStore annotations2;
    SectionStore sections2;
    AxesStore axes2;
    MaterialRepository materials2;
    AssetRepository assets2;
    StyleStore style2;
    ShadowStore shadow2;
    FogStore fog2;
    CameraState cameraOut;
    DocumentMeta metaOut;
    const ReadResult result = plnr::io::readDocument(bytes, geometry2, tags2, guides2, annotations2, sections2, axes2,
                                                      materials2, assets2, style2, shadow2, fog2, &cameraOut, &metaOut);
    ASSERT_TRUE(result.ok) << result.error.toStdString();
    EXPECT_EQ(cameraOut.projection, plnr::events::Projection::TwoPoint);
}

}  // namespace
