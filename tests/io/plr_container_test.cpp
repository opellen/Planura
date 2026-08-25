#include "io/plr_container.h"

#include <string>

#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>

#include <miniz.h>

#include <gtest/gtest.h>

// Covers io::assembleContainer/io::openContainer directly -- container
// mechanics (raw-vs-zip decision, entry order, determinism, ZIP structure),
// not the MaterialRepository round trip (see plr_reader_test.cpp). Links plnr_miniz
// directly to assert entry order independently of io::openContainer.
namespace {

using plnr::agent::Asset;
using plnr::agent::AssetRepository;
using plnr::io::assembleContainer;
using plnr::io::openContainer;
using plnr::io::OpenedContainer;

QJsonObject materialWithTexture(double id, const QString& assetHash, double tileW, double tileH) {
    QJsonObject material;
    material["id"] = id;
    material["name"] = QStringLiteral("Mat");
    material["color"] = QJsonArray{0.5, 0.5, 0.5};
    material["opacity"] = 1.0;
    QJsonObject texture;
    texture["assetHash"] = assetHash;
    texture["tileW"] = tileW;
    texture["tileH"] = tileH;
    material["texture"] = texture;
    material["pbr"] = QJsonValue();
    return material;
}

QJsonObject baseDoc() {
    QJsonObject doc;
    doc["format"] = QStringLiteral("planura");
    doc["formatVersion"] = 1;
    doc["definitions"] = QJsonArray{};
    doc["tags"] = QJsonArray{};
    doc["hidden"] = QJsonArray{};
    doc["guides"] = QJsonArray{};
    doc["dimensions"] = QJsonArray{};
    doc["texts"] = QJsonArray{};
    doc["sectionPlanes"] = QJsonArray{};
    doc["curves"] = QJsonValue();
    doc["materials"] = QJsonValue();
    return doc;
}

TEST(PlrContainerTest, NoReferencedAssetsProducesByteIdenticalRawJson) {
    const QJsonDocument doc(baseDoc());  // materials: null -- no assets referenced
    AssetRepository assets;

    const QByteArray container = assembleContainer(doc, assets);
    EXPECT_EQ(container, doc.toJson(QJsonDocument::Indented));
    EXPECT_FALSE(container.startsWith("PK"));
}

TEST(PlrContainerTest, MaterialWithNullTextureProducesByteIdenticalRawJson) {
    QJsonObject root = baseDoc();
    QJsonObject material;
    material["id"] = 1.0;
    material["name"] = QStringLiteral("Untextured");
    material["color"] = QJsonArray{0.1, 0.2, 0.3};
    material["opacity"] = 1.0;
    material["texture"] = QJsonValue();
    material["pbr"] = QJsonValue();
    root["materials"] = QJsonArray{material};
    root["materialAssignments"] = QJsonArray{};
    const QJsonDocument doc(root);
    AssetRepository assets;

    const QByteArray container = assembleContainer(doc, assets);
    EXPECT_EQ(container, doc.toJson(QJsonDocument::Indented));
}

TEST(PlrContainerTest, TexturedMaterialProducesZipStartingWithPk) {
    AssetRepository assets;
    const std::string hash = assets.add("texture bytes", "png");

    QJsonObject root = baseDoc();
    root["materials"] = QJsonArray{materialWithTexture(1.0, QString::fromStdString(hash), 1.0, 1.0)};
    root["materialAssignments"] = QJsonArray{};
    const QJsonDocument doc(root);

    const QByteArray container = assembleContainer(doc, assets);
    EXPECT_TRUE(container.startsWith("PK\x03\x04"));
    EXPECT_NE(container, doc.toJson(QJsonDocument::Indented));
}

TEST(PlrContainerTest, EntryOrderIsModelJsonFirstThenAssetsSortedByHash) {
    AssetRepository assets;
    // Two distinct hashes, added in an order that does NOT already sort
    // correctly -- exercises the sort, not just insertion order.
    const std::string hashB = assets.add("content B", "png");
    const std::string hashA = assets.add("content A", "jpg");
    ASSERT_NE(hashA, hashB);

    const QString entryA = QStringLiteral("assets/") + QString::fromStdString(hashA) + QStringLiteral(".jpg");
    const QString entryB = QStringLiteral("assets/") + QString::fromStdString(hashB) + QStringLiteral(".png");
    // Ascending sort -- the smaller hash's entry comes first (index 1, right
    // after model.json at index 0).
    const QString expectedSecond = hashA < hashB ? entryA : entryB;
    const QString expectedThird = hashA < hashB ? entryB : entryA;

    QJsonObject root = baseDoc();
    root["materials"] = QJsonArray{materialWithTexture(1.0, QString::fromStdString(hashA), 1.0, 1.0),
                                    materialWithTexture(2.0, QString::fromStdString(hashB), 1.0, 1.0)};
    root["materialAssignments"] = QJsonArray{};
    const QJsonDocument doc(root);

    const QByteArray container = assembleContainer(doc, assets);

    mz_zip_archive zip;
    mz_zip_zero_struct(&zip);
    ASSERT_TRUE(mz_zip_reader_init_mem(&zip, container.constData(), static_cast<size_t>(container.size()), 0));
    ASSERT_EQ(mz_zip_reader_get_num_files(&zip), 3u);  // model.json + 2 assets

    char nameBuf[256];
    mz_zip_reader_get_filename(&zip, 0, nameBuf, sizeof(nameBuf));
    EXPECT_STREQ(nameBuf, "model.json");
    mz_zip_reader_get_filename(&zip, 1, nameBuf, sizeof(nameBuf));
    EXPECT_EQ(QString::fromUtf8(nameBuf), expectedSecond);
    mz_zip_reader_get_filename(&zip, 2, nameBuf, sizeof(nameBuf));
    EXPECT_EQ(QString::fromUtf8(nameBuf), expectedThird);
    mz_zip_reader_end(&zip);
}

TEST(PlrContainerTest, SameStateProducesByteIdenticalZipAcrossTwoCalls) {
    AssetRepository assets;
    const std::string hash = assets.add("texture bytes", "png");
    QJsonObject root = baseDoc();
    root["materials"] = QJsonArray{materialWithTexture(1.0, QString::fromStdString(hash), 1.0, 1.0)};
    root["materialAssignments"] = QJsonArray{};
    const QJsonDocument doc(root);

    const QByteArray containerA = assembleContainer(doc, assets);
    const QByteArray containerB = assembleContainer(doc, assets);
    EXPECT_FALSE(containerA.isEmpty());
    EXPECT_EQ(containerA, containerB);
}

TEST(PlrContainerTest, OpenContainerRoundTripsJsonBytesAndAssets) {
    AssetRepository assets;
    const std::string hash = assets.add("texture bytes", "png");
    QJsonObject root = baseDoc();
    root["materials"] = QJsonArray{materialWithTexture(1.0, QString::fromStdString(hash), 1.0, 1.0)};
    root["materialAssignments"] = QJsonArray{};
    const QJsonDocument doc(root);
    const QByteArray expectedJsonBytes = doc.toJson(QJsonDocument::Indented);

    const QByteArray container = assembleContainer(doc, assets);
    const OpenedContainer opened = openContainer(container);
    ASSERT_TRUE(opened.ok) << opened.error.toStdString();
    EXPECT_EQ(opened.jsonBytes, expectedJsonBytes);
    ASSERT_EQ(opened.assets.size(), 1u);
    auto it = opened.assets.find(hash);
    ASSERT_NE(it, opened.assets.end());
    EXPECT_EQ(it->second.bytes, "texture bytes");
    EXPECT_EQ(it->second.ext, "png");
}

TEST(PlrContainerTest, OpenContainerRejectsCorruptBytes) {
    const QByteArray garbage("PK\x03\x04this is not a real zip archive at all, just garbage bytes", 40);
    const OpenedContainer opened = openContainer(garbage);
    EXPECT_FALSE(opened.ok);
    EXPECT_FALSE(opened.error.isEmpty());
}

// A structurally valid ZIP missing the required "model.json" entry (hand-
// built via miniz's writer API, bypassing assembleContainer) is rejected too.
TEST(PlrContainerTest, OpenContainerRejectsArchiveMissingModelJson) {
    mz_zip_archive zip;
    mz_zip_zero_struct(&zip);
    ASSERT_TRUE(mz_zip_writer_init_heap(&zip, 0, 1024));
    const QByteArray payload("not model.json");
    ASSERT_TRUE(mz_zip_writer_add_mem(&zip, "assets/deadbeefcafef00d.png", payload.constData(),
                                       static_cast<size_t>(payload.size()), MZ_NO_COMPRESSION));
    void* heapBuf = nullptr;
    size_t heapSize = 0;
    ASSERT_TRUE(mz_zip_writer_finalize_heap_archive(&zip, &heapBuf, &heapSize));
    const QByteArray container(static_cast<const char*>(heapBuf), static_cast<int>(heapSize));
    mz_free(heapBuf);
    mz_zip_writer_end(&zip);

    const OpenedContainer opened = openContainer(container);
    EXPECT_FALSE(opened.ok);
    EXPECT_FALSE(opened.error.isEmpty());
}

}  // namespace
