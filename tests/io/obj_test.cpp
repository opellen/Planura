#include "io/obj_writer.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QIODevice>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>

#include <ordo/core/kernel.h>

#include "agent/command/annotation_commands.h"
#include "agent/document_store.h"
#include "agent/events.h"
#include "agent/geometry_api.h"
#include "agent/command/obj_commands.h"
#include "agent/tag_store.h"
#include "agent/command/undo_commands.h"
#include "agent/undo_store.h"
#include "io/obj_reader.h"

#include <geo/entity.h>
#include <geo/model.h>
#include <geo/scene.h>
#include <geo/vec3.h>

#include <gtest/gtest.h>

// OBJ export/import: writer golden/visibility, reader grammar/errors, export->readObj->import round-trip,
// command-level import/undo/redo + dirty tracking.
namespace {

using ordo::core::Kernel;
using plnr::agent::DocumentStore;
using plnr::agent::ExportObjCommand;
using plnr::agent::GeometryChangedCommand;
using plnr::agent::GeometryApi;
using plnr::agent::ImportObjCommand;
using plnr::agent::kDocumentStoreName;
using plnr::agent::kGeometryApiName;
using plnr::agent::kTagStoreName;
using plnr::agent::kUndoStoreName;
using plnr::agent::RedoCommand;
using plnr::agent::TagStore;
using plnr::agent::UndoCaptureCommand;
using plnr::agent::UndoCommand;
using plnr::agent::UndoStore;
using plnr::events::EntityRef;
using plnr::geo::Definition;
using plnr::geo::EntityKind;
using plnr::geo::Id;
using plnr::geo::Instance;
using plnr::geo::kInvalidId;
using plnr::geo::kRootDefinitionId;
using plnr::geo::Model;
using plnr::geo::Transform;
using plnr::geo::Vec3;
using plnr::geo::Vertex;

// Mirrors obj_writer.cpp's formatDouble (17-sig-digit round-trip); duplicated, it's unreachable across TUs.
QString fmtNum(double v) {
    QString s = QString::number(v, 'g', 17);
    if (s == QStringLiteral("-0")) {
        return QStringLiteral("0");
    }
    return s;
}

QString vecLine(const char* tag, const Vec3& v) {
    return QString::fromLatin1(tag) + QStringLiteral(" ") + fmtNum(v.x) + QStringLiteral(" ") + fmtNum(v.y) +
           QStringLiteral(" ") + fmtNum(v.z);
}

// Re-derives g/v/vn/f lines from live model state rather than hand-predicting ids; loop order agrees with face->normal.
QStringList buildGroupLines(const QString& path, const Model& model, Id faceId, const Transform& xf, int& nextV,
                             int& nextN) {
    QStringList lines;
    lines << (QStringLiteral("g ") + path);

    const std::vector<Id> loop = model.faceVertexLoop(faceId);
    std::vector<Id> sortedIds = loop;
    std::sort(sortedIds.begin(), sortedIds.end());

    std::unordered_map<Id, int> localIndex;
    for (Id id : sortedIds) {
        const Vertex* v = model.vertex(id);
        lines << vecLine("v", xf.apply(v->pos));
        localIndex[id] = nextV++;
    }

    const plnr::geo::Face* face = model.face(faceId);
    const Vec3 worldNormal = plnr::geo::normalized(xf.applyVector(face->normal));
    lines << vecLine("vn", worldNormal);
    const int n = nextN++;

    QString f = QStringLiteral("f");
    for (Id id : loop) {
        f += QStringLiteral(" ") + QString::number(localIndex.at(id)) + QStringLiteral("//") + QString::number(n);
    }
    lines << f;
    return lines;
}

// -- Writer tests -----------------------------------------------------------

class ObjWriterTest : public ::testing::Test {
protected:
    void SetUp() override {
        kernel.registerAgent(std::make_shared<GeometryApi>());
        kernel.registerAgent(std::make_shared<TagStore>());
        geometry = kernel.agentAs<GeometryApi>(kGeometryApiName);
        tags = kernel.agentAs<TagStore>(kTagStoreName);
    }

    Kernel kernel;
    std::shared_ptr<GeometryApi> geometry;
    std::shared_ptr<TagStore> tags;
};

// Golden export: root geometry plus two group instances (pure translation, pure rotation); rotation
// catches col/row mixups that translation can't.
TEST_F(ObjWriterTest, GoldenExportRootPlusTranslatedAndRotatedInstances) {
    // Root triangle clear of the two source triangles (no coincidence-merge at draw time).
    ASSERT_TRUE(geometry->addPolyline({{0, 0, 0}, {4, 0, 0}, {0, 4, 0}}, true));
    ASSERT_EQ(geometry->model().faces().size(), 1u);
    const Id rootFaceId = geometry->model().faces().begin()->first;

    // "Trans" source triangle -> group -> pure translation.
    ASSERT_TRUE(geometry->addPolyline({{50, 0, 0}, {51, 0, 0}, {50, 1, 0}}, true));
    Id transSourceFaceId = kInvalidId;
    for (const auto& [id, face] : geometry->model().faces()) {
        (void)face;
        if (id != rootFaceId) transSourceFaceId = id;
    }
    ASSERT_NE(transSourceFaceId, kInvalidId);
    const Id transInstId = geometry->makeGroup({EntityRef{EntityKind::Face, transSourceFaceId}}, false, "Trans");
    ASSERT_NE(transInstId, kInvalidId);
    const Transform transXf{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {10, 0, 5}};
    ASSERT_TRUE(geometry->setInstanceTransform(transInstId, transXf));

    // "Rot" source triangle -> group -> 90 deg about Z via a hand-built exact matrix
    // (Transform::rotation's trig would add float noise to the golden text).
    ASSERT_TRUE(geometry->addPolyline({{0, 50, 0}, {1, 50, 0}, {0, 51, 0}}, true));
    Id rotSourceFaceId = kInvalidId;
    for (const auto& [id, face] : geometry->model().faces()) {
        (void)face;
        if (id != rootFaceId) rotSourceFaceId = id;
    }
    ASSERT_NE(rotSourceFaceId, kInvalidId);
    const Id rotInstId = geometry->makeGroup({EntityRef{EntityKind::Face, rotSourceFaceId}}, false, "Rot");
    ASSERT_NE(rotInstId, kInvalidId);
    const Transform rotXf{{0, 1, 0}, {-1, 0, 0}, {0, 0, 1}, {0, 0, 0}};
    ASSERT_TRUE(geometry->setInstanceTransform(rotInstId, rotXf));

    const Definition& rootDef = geometry->scene().root();
    ASSERT_EQ(rootDef.children.size(), 2u);
    const Instance& transInst = rootDef.children[0];  // grouped first -> appended first
    const Instance& rotInst = rootDef.children[1];
    ASSERT_EQ(transInst.id, transInstId);
    ASSERT_EQ(rotInst.id, rotInstId);

    const Definition* transDef = geometry->scene().definition(transInst.definitionId);
    const Definition* rotDef = geometry->scene().definition(rotInst.definitionId);
    ASSERT_NE(transDef, nullptr);
    ASSERT_NE(rotDef, nullptr);
    ASSERT_EQ(transDef->model.faces().size(), 1u);
    ASSERT_EQ(rotDef->model.faces().size(), 1u);
    const Id transFaceId = transDef->model.faces().begin()->first;
    const Id rotFaceId = rotDef->model.faces().begin()->first;

    int nextV = 1;
    int nextN = 1;
    QStringList expectedLines;
    expectedLines += buildGroupLines(QStringLiteral("root"), geometry->model(), rootFaceId, Transform::identity(),
                                      nextV, nextN);
    expectedLines +=
        buildGroupLines(QStringLiteral("root/Trans"), transDef->model, transFaceId, transXf, nextV, nextN);
    expectedLines += buildGroupLines(QStringLiteral("root/Rot"), rotDef->model, rotFaceId, rotXf, nextV, nextN);
    const std::string expected = (expectedLines.join(QChar('\n')) + QChar('\n')).toStdString();

    const QByteArray actual = plnr::io::writeObj(*geometry, *tags);
    EXPECT_EQ(actual.toStdString(), expected);
}

TEST_F(ObjWriterTest, ExportSkipsHiddenRootFace) {
    ASSERT_TRUE(geometry->addPolyline({{0, 0, 0}, {4, 0, 0}, {0, 4, 0}}, true));
    ASSERT_TRUE(geometry->addPolyline({{20, 0, 0}, {21, 0, 0}, {20, 1, 0}}, true));
    ASSERT_EQ(geometry->model().faces().size(), 2u);

    Id visibleFaceId = kInvalidId;
    Id hiddenFaceId = kInvalidId;
    for (const auto& [id, face] : geometry->model().faces()) {
        (void)face;
        const std::vector<Id> loop = geometry->model().faceVertexLoop(id);
        const Vec3 p0 = geometry->model().vertex(loop[0])->pos;
        (p0.x < 10.0 ? visibleFaceId : hiddenFaceId) = id;
    }
    ASSERT_NE(visibleFaceId, kInvalidId);
    ASSERT_NE(hiddenFaceId, kInvalidId);
    ASSERT_TRUE(geometry->setHidden({EntityRef{EntityKind::Face, hiddenFaceId}}, true));

    const QByteArray obj = plnr::io::writeObj(*geometry, *tags);
    const QString text = QString::fromUtf8(obj);
    EXPECT_EQ(text.count(QStringLiteral("f ")), 1);
    EXPECT_TRUE(text.contains(QStringLiteral("v 0 0 0")));
    EXPECT_FALSE(text.contains(QStringLiteral("v 20 0 0")));
}

TEST_F(ObjWriterTest, ExportSkipsHiddenRootInstanceWholeSubtree) {
    ASSERT_TRUE(geometry->addPolyline({{50, 0, 0}, {51, 0, 0}, {50, 1, 0}}, true));
    const Id faceId = geometry->model().faces().begin()->first;
    const Id instId = geometry->makeGroup({EntityRef{EntityKind::Face, faceId}}, false, "Hidden");
    ASSERT_NE(instId, kInvalidId);
    ASSERT_TRUE(geometry->setHidden({EntityRef{EntityKind::Instance, instId}}, true));

    const QByteArray obj = plnr::io::writeObj(*geometry, *tags);
    const QString text = QString::fromUtf8(obj);
    EXPECT_FALSE(text.contains(QStringLiteral("g root/Hidden")));
    EXPECT_TRUE(text.trimmed().isEmpty());  // root itself has no loose geometry either
}

TEST_F(ObjWriterTest, ExportSkipsFaceOnInvisibleTag) {
    ASSERT_TRUE(geometry->addPolyline({{0, 0, 0}, {4, 0, 0}, {0, 4, 0}}, true));
    const Id faceId = geometry->model().faces().begin()->first;
    const std::uint64_t hiddenTagId = tags->createTag("HiddenLayer");
    ASSERT_TRUE(tags->assignTag({EntityRef{EntityKind::Face, faceId}}, hiddenTagId));
    ASSERT_TRUE(tags->setTagVisible(hiddenTagId, false));

    const QByteArray obj = plnr::io::writeObj(*geometry, *tags);
    EXPECT_TRUE(QString::fromUtf8(obj).trimmed().isEmpty());
}

// -- Reader tests -------------------------------------------------------------

TEST(ObjReaderTest, ParsesTriangleAndQuadFaces) {
    const QByteArray obj = "v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\nf 1 2 3\nf 1 2 3 4\n";
    const plnr::io::ReadObjResult r = plnr::io::readObj(obj);
    ASSERT_TRUE(r.ok) << r.error.toStdString();
    ASSERT_EQ(r.mesh.vertices.size(), 4u);
    ASSERT_EQ(r.mesh.faces.size(), 2u);
    EXPECT_EQ(r.mesh.faces[0], (std::vector<std::size_t>{0, 1, 2}));
    EXPECT_EQ(r.mesh.faces[1], (std::vector<std::size_t>{0, 1, 2, 3}));
}

TEST(ObjReaderTest, ToleratesVSlashVtAndVSlashSlashVnIndexForms) {
    const QByteArray obj1 = "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1/1 2/2 3/3\n";
    const plnr::io::ReadObjResult r1 = plnr::io::readObj(obj1);
    ASSERT_TRUE(r1.ok) << r1.error.toStdString();
    EXPECT_EQ(r1.mesh.faces[0], (std::vector<std::size_t>{0, 1, 2}));

    const QByteArray obj2 = "v 0 0 0\nv 1 0 0\nv 0 1 0\nvn 0 0 1\nf 1//1 2//1 3//1\n";
    const plnr::io::ReadObjResult r2 = plnr::io::readObj(obj2);
    ASSERT_TRUE(r2.ok) << r2.error.toStdString();
    EXPECT_EQ(r2.mesh.faces[0], (std::vector<std::size_t>{0, 1, 2}));

    const QByteArray obj3 = "v 0 0 0\nv 1 0 0\nv 0 1 0\nvt 0 0\nvn 0 0 1\nf 1/1/1 2/1/1 3/1/1\n";
    const plnr::io::ReadObjResult r3 = plnr::io::readObj(obj3);
    ASSERT_TRUE(r3.ok) << r3.error.toStdString();
    EXPECT_EQ(r3.mesh.faces[0], (std::vector<std::size_t>{0, 1, 2}));
}

TEST(ObjReaderTest, NegativeFaceIndicesResolveRelativeToCurrentVertexCount) {
    const QByteArray obj = "v 0 0 0\nv 1 0 0\nv 0 1 0\nf -3 -2 -1\n";
    const plnr::io::ReadObjResult r = plnr::io::readObj(obj);
    ASSERT_TRUE(r.ok) << r.error.toStdString();
    ASSERT_EQ(r.mesh.faces.size(), 1u);
    EXPECT_EQ(r.mesh.faces[0], (std::vector<std::size_t>{0, 1, 2}));
}

TEST(ObjReaderTest, TreatsCommentsGroupObjectMtllibUsemtlAsNoOps) {
    const QByteArray obj =
        "# a comment\n"
        "mtllib foo.mtl\n"
        "o MyObject\n"
        "g MyGroup\n"
        "usemtl Red\n"
        "v 0 0 0\n"
        "v 1 0 0\n"
        "v 0 1 0\n"
        "f 1 2 3\n";
    const plnr::io::ReadObjResult r = plnr::io::readObj(obj);
    ASSERT_TRUE(r.ok) << r.error.toStdString();
    EXPECT_EQ(r.mesh.vertices.size(), 3u);
    EXPECT_EQ(r.mesh.faces.size(), 1u);
}

// A 5-vertex `f` becomes ONE 5-index face, not triangulated (n-gons are native).
TEST(ObjReaderTest, FiveVertexFaceStaysOnePolygonNotTriangulated) {
    const QByteArray obj = "v 0 0 0\nv 2 0 0\nv 3 1 0\nv 1 2 0\nv -1 1 0\nf 1 2 3 4 5\n";
    const plnr::io::ReadObjResult r = plnr::io::readObj(obj);
    ASSERT_TRUE(r.ok) << r.error.toStdString();
    ASSERT_EQ(r.mesh.faces.size(), 1u);
    EXPECT_EQ(r.mesh.faces[0], (std::vector<std::size_t>{0, 1, 2, 3, 4}));
}

TEST(ObjReaderErrorTest, EmptyFileIsAnError) {
    const plnr::io::ReadObjResult r = plnr::io::readObj(QByteArray());
    EXPECT_FALSE(r.ok);
    EXPECT_EQ(r.errorLine, 0);
}

TEST(ObjReaderErrorTest, WhitespaceOnlyFileIsAnError) {
    const plnr::io::ReadObjResult r = plnr::io::readObj(QByteArray("   \n\t\n"));
    EXPECT_FALSE(r.ok);
}

TEST(ObjReaderErrorTest, MalformedFaceTooFewVertices) {
    const QByteArray obj = "v 0 0 0\nv 1 0 0\nf 1 2\n";
    const plnr::io::ReadObjResult r = plnr::io::readObj(obj);
    EXPECT_FALSE(r.ok);
    EXPECT_EQ(r.errorLine, 3);
}

TEST(ObjReaderErrorTest, BadVertexIndexOutOfRange) {
    const QByteArray obj = "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 99\n";
    const plnr::io::ReadObjResult r = plnr::io::readObj(obj);
    EXPECT_FALSE(r.ok);
    EXPECT_EQ(r.errorLine, 4);
}

TEST(ObjReaderErrorTest, NonNumericVertexCoordinateIsAnError) {
    const QByteArray obj = "v x 0 0\n";
    const plnr::io::ReadObjResult r = plnr::io::readObj(obj);
    EXPECT_FALSE(r.ok);
    EXPECT_EQ(r.errorLine, 1);
}

// -- Structural round-trip: export -> readObj -> importMesh -----------------

TEST(ObjRoundTripTest, ExportReadObjImportPreservesGeometryExactly) {
    Kernel writerKernel;
    writerKernel.registerAgent(std::make_shared<GeometryApi>());
    writerKernel.registerAgent(std::make_shared<TagStore>());
    auto geometry = writerKernel.agentAs<GeometryApi>(kGeometryApiName);
    auto tags = writerKernel.agentAs<TagStore>(kTagStoreName);

    ASSERT_TRUE(geometry->addRectangle({0, 0, 0}, {4, 3, 0}));
    ASSERT_EQ(geometry->model().faces().size(), 1u);

    const QByteArray objBytes = plnr::io::writeObj(*geometry, *tags);
    const plnr::io::ReadObjResult result = plnr::io::readObj(objBytes);
    ASSERT_TRUE(result.ok) << result.error.toStdString();
    ASSERT_EQ(result.mesh.faces.size(), 2u);  // the rectangle triangulates to 2 triangles on export
    for (const std::vector<std::size_t>& face : result.mesh.faces) {
        EXPECT_EQ(face.size(), 3u);
    }

    // Axis conversion is the identity both ways (see obj_writer.h): every parsed vertex matches
    // one of the rectangle's own corners.
    const std::vector<Vec3> expectedCorners{{0, 0, 0}, {4, 0, 0}, {4, 3, 0}, {0, 3, 0}};
    for (const Vec3& corner : expectedCorners) {
        bool found = false;
        for (const Vec3& v : result.mesh.vertices) {
            if (plnr::geo::almostEqual(v, corner, 1e-9)) {
                found = true;
                break;
            }
        }
        EXPECT_TRUE(found) << "missing corner (" << corner.x << "," << corner.y << "," << corner.z << ")";
    }

    Kernel importKernel;
    importKernel.registerAgent(std::make_shared<GeometryApi>());
    auto geometry2 = importKernel.agentAs<GeometryApi>(kGeometryApiName);
    const Id instId = geometry2->importMesh("Test", result.mesh.vertices, result.mesh.faces);
    ASSERT_NE(instId, kInvalidId);

    const Instance* inst = geometry2->scene().findInstance(kRootDefinitionId, instId);
    ASSERT_NE(inst, nullptr);
    const Definition* def = geometry2->scene().definition(inst->definitionId);
    ASSERT_NE(def, nullptr);
    EXPECT_EQ(def->model.faces().size(), result.mesh.faces.size());
    ASSERT_EQ(def->model.vertices().size(), result.mesh.vertices.size());

    for (std::size_t i = 0; i < result.mesh.vertices.size(); ++i) {
        const Vertex* v = def->model.vertex(static_cast<Id>(i) + 1);
        ASSERT_NE(v, nullptr);
        EXPECT_NEAR(v->pos.x, result.mesh.vertices[i].x, 1e-9);
        EXPECT_NEAR(v->pos.y, result.mesh.vertices[i].y, 1e-9);
        EXPECT_NEAR(v->pos.z, result.mesh.vertices[i].z, 1e-9);
    }
}

// -- Command-level: ImportObjRequested/ExportObjRequested through a kernel --

// Minimal kernel: GeometryApi/UndoStore (Transaction), DocumentStore (dirty tracking), TagStore
// (ExportObjCommand::execute); other Agents are absent.
class ObjCommandTest : public ::testing::Test {
protected:
    void SetUp() override {
        kernel.registerAgent(std::make_shared<GeometryApi>());
        kernel.registerAgent(std::make_shared<TagStore>());
        kernel.registerAgent(std::make_shared<DocumentStore>());
        kernel.registerAgent(std::make_shared<UndoStore>());

        kernel.registerCommand<plnr::events::ImportObjRequested,
                                UndoCaptureCommand<ImportObjCommand, plnr::events::ImportObjRequested>>();
        kernel.registerCommand<plnr::events::ExportObjRequested, ExportObjCommand>();
        kernel.registerCommand<plnr::events::GeometryChanged, GeometryChangedCommand>();
        kernel.registerCommand<plnr::events::UndoRequested, UndoCommand>();
        kernel.registerCommand<plnr::events::RedoRequested, RedoCommand>();

        geometry = kernel.agentAs<GeometryApi>(kGeometryApiName);
        document = kernel.agentAs<DocumentStore>(kDocumentStoreName);
        undo = kernel.agentAs<UndoStore>(kUndoStoreName);
        ASSERT_NE(geometry, nullptr);
        ASSERT_NE(document, nullptr);
        ASSERT_NE(undo, nullptr);
    }

    std::string writeTempObj(const char* basename, const QByteArray& contents) {
        const QString path = QDir(tempDir.path()).filePath(QString::fromUtf8(basename));
        QFile f(path);
        EXPECT_TRUE(f.open(QIODevice::WriteOnly));
        f.write(contents);
        f.close();
        return path.toStdString();
    }

    Kernel kernel;
    QTemporaryDir tempDir;
    std::shared_ptr<GeometryApi> geometry;
    std::shared_ptr<DocumentStore> document;
    std::shared_ptr<UndoStore> undo;
};

TEST_F(ObjCommandTest, ImportCreatesDefinitionAndInstance_UndoRemoves_RedoRestores) {
    const std::string path = writeTempObj("import.obj", "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");

    ASSERT_EQ(geometry->scene().root().children.size(), 0u);
    kernel.send(plnr::events::ImportObjRequested{path});

    ASSERT_EQ(geometry->scene().root().children.size(), 1u);
    const Id instId = geometry->scene().root().children[0].id;
    const Id defId = geometry->scene().root().children[0].definitionId;
    const Definition* def = geometry->scene().definition(defId);
    ASSERT_NE(def, nullptr);
    EXPECT_EQ(def->model.faces().size(), 1u);
    EXPECT_TRUE(undo->canUndo());

    kernel.send(plnr::events::UndoRequested{});
    EXPECT_EQ(geometry->scene().root().children.size(), 0u);
    EXPECT_EQ(geometry->scene().definition(defId), nullptr);
    EXPECT_TRUE(undo->canRedo());

    kernel.send(plnr::events::RedoRequested{});
    ASSERT_EQ(geometry->scene().root().children.size(), 1u);
    EXPECT_EQ(geometry->scene().root().children[0].id, instId);
    const Definition* defAfterRedo = geometry->scene().definition(defId);
    ASSERT_NE(defAfterRedo, nullptr);
    EXPECT_EQ(defAfterRedo->model.faces().size(), 1u);
}

TEST_F(ObjCommandTest, ImportMarksDirty_ExportDoesNot) {
    const std::string importPath = writeTempObj("import_dirty.obj", "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");

    EXPECT_FALSE(document->dirty());
    kernel.send(plnr::events::ImportObjRequested{importPath});
    EXPECT_TRUE(document->dirty());

    // Isolate the export check: reset dirty via resetNew().
    document->resetNew();
    ASSERT_FALSE(document->dirty());

    const QString exportPath = QDir(tempDir.path()).filePath(QStringLiteral("export_test.obj"));
    kernel.send(plnr::events::ExportObjRequested{exportPath.toStdString()});
    EXPECT_FALSE(document->dirty());
    EXPECT_TRUE(QFile::exists(exportPath));
}

TEST_F(ObjCommandTest, ImportOfMissingFileReportsFailureAndCreatesNothing) {
    const QString missingPath = QDir(tempDir.path()).filePath(QStringLiteral("does_not_exist.obj"));
    kernel.send(plnr::events::ImportObjRequested{missingPath.toStdString()});

    EXPECT_EQ(geometry->scene().root().children.size(), 0u);
    EXPECT_FALSE(document->dirty());  // the failed import's transaction captured no real change
    EXPECT_FALSE(undo->canUndo());
}

// -- async import/export split --

class AsyncObjCommandTest : public ObjCommandTest {
protected:
    void SetUp() override {
        ObjCommandTest::SetUp();
        kernel.registerCommand<plnr::events::ImportObjDataReady,
                                UndoCaptureCommand<plnr::agent::ImportObjDataCommand, plnr::events::ImportObjDataReady>>();
        kernel.registerCommand<plnr::events::ExportObjSnapshotRequested, plnr::agent::ExportObjSnapshotCommand>();
        kernel.dispatcher().subscribe<plnr::events::ObjBytesReady>(
            &published, [this](const plnr::events::ObjBytesReady& e) { published.push_back(e); });
        kernel.dispatcher().subscribe<plnr::events::DocumentIoFailed>(
            &failures, [this](const plnr::events::DocumentIoFailed& e) { failures.push_back(e.message); });
    }

    std::vector<plnr::events::ObjBytesReady> published;
    std::vector<std::string> failures;
};

TEST_F(AsyncObjCommandTest, ImportDataReadyAppliesUndoablyAndMarksDirty) {
    auto parsed = std::make_shared<plnr::io::ReadObjResult>(
        plnr::io::readObj("v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n"));
    ASSERT_TRUE(parsed->ok);

    kernel.send(plnr::events::ImportObjDataReady{"x.obj", "x", parsed});
    ASSERT_EQ(geometry->scene().root().children.size(), 1u);
    EXPECT_TRUE(document->dirty());
    EXPECT_TRUE(undo->canUndo());
    EXPECT_TRUE(failures.empty());

    kernel.send(plnr::events::UndoRequested{});
    EXPECT_EQ(geometry->scene().root().children.size(), 0u);
}

TEST_F(AsyncObjCommandTest, ImportDataReadyWithoutGeometryReportsFailure) {
    auto parsed = std::make_shared<plnr::io::ReadObjResult>(plnr::io::readObj("v 0 0 0\n"));
    ASSERT_TRUE(parsed->ok);
    kernel.send(plnr::events::ImportObjDataReady{"e.obj", "e", parsed});
    EXPECT_EQ(geometry->scene().root().children.size(), 0u);
    EXPECT_EQ(failures.size(), 1u);
}

TEST_F(AsyncObjCommandTest, ExportSnapshotPublishesBytesWithoutTouchingDocument) {
    const std::string path = writeTempObj("src.obj", "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");
    kernel.send(plnr::events::ImportObjRequested{path});
    document->resetNew();

    const std::string out = (QDir(tempDir.path()).filePath(QStringLiteral("o.obj"))).toStdString();
    kernel.send(plnr::events::ExportObjSnapshotRequested{out});
    ASSERT_EQ(published.size(), 1u);
    ASSERT_NE(published[0].bytes, nullptr);
    EXPECT_EQ(published[0].path, out);
    EXPECT_EQ(*published[0].bytes, plnr::io::writeObj(*geometry, *kernel.agentAs<TagStore>(kTagStoreName)));
    EXPECT_FALSE(document->dirty());
    EXPECT_FALSE(QFile::exists(QString::fromStdString(out)));  // snapshot step never writes
}

}  // namespace
