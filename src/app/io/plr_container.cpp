#include "io/plr_container.h"

#include <algorithm>
#include <set>
#include <utility>

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>

#include <miniz.h>

// miniz heap-backed APIs throughout: everything stays in memory, no temp files.
namespace plnr::io {

namespace {

// Fixed zero timestamp for every ZIP entry: same agent state -> byte-identical ZIP.
MZ_TIME_T fixedEntryTime() {
    return static_cast<MZ_TIME_T>(0);
}

// Distinct non-empty texture.assetHash values in doc's "materials", sorted ascending.
// Best-effort scan for the raw-vs-zip decision; malformed entries are skipped.
std::vector<std::string> referencedAssetHashes(const QJsonDocument& doc) {
    std::set<std::string> hashes;
    const QJsonValue materialsVal = doc.object().value("materials");
    if (!materialsVal.isArray()) {
        return {};
    }
    for (const QJsonValue& entry : materialsVal.toArray()) {
        if (!entry.isObject()) continue;
        const QJsonValue textureVal = entry.toObject().value("texture");
        if (!textureVal.isObject()) continue;
        const QJsonValue hashVal = textureVal.toObject().value("assetHash");
        if (!hashVal.isString()) continue;
        const QString hash = hashVal.toString();
        if (!hash.isEmpty()) {
            hashes.insert(hash.toStdString());
        }
    }
    return std::vector<std::string>(hashes.begin(), hashes.end());
}

}  // namespace

std::vector<AssetBlob> collectAssetBlobs(const agent::AssetRepository& assetStore) {
    std::vector<AssetBlob> blobs;
    for (const std::string& hash : assetStore.hashes()) {
        const agent::Asset* asset = assetStore.get(hash);
        if (asset != nullptr) blobs.push_back(AssetBlob{hash, *asset});
    }
    return blobs;
}

QByteArray assembleContainer(const QJsonDocument& doc, const agent::AssetRepository& assetStore) {
    return assembleContainer(doc, collectAssetBlobs(assetStore));
}

QByteArray assembleContainer(const QJsonDocument& doc, const std::vector<AssetBlob>& blobs) {
    const QByteArray jsonBytes = doc.toJson(QJsonDocument::Indented);

    const std::vector<std::string> referenced = referencedAssetHashes(doc);
    if (referenced.empty()) {
        return jsonBytes;  // no assets -- byte-identical raw-JSON contract
    }

    mz_zip_archive zip;
    mz_zip_zero_struct(&zip);
    if (!mz_zip_writer_init_heap(&zip, 0, static_cast<size_t>(jsonBytes.size()) + 4096)) {
        // init_heap fails only on allocation failure; no error channel here, so fall back to raw bytes.
        return jsonBytes;
    }

    MZ_TIME_T fixedTime = fixedEntryTime();
    bool ok = mz_zip_writer_add_mem_ex_v2(&zip, "model.json", jsonBytes.constData(),
                                          static_cast<size_t>(jsonBytes.size()), nullptr, 0, MZ_DEFAULT_COMPRESSION, 0,
                                          0, &fixedTime, nullptr, 0, nullptr, 0);

    for (const std::string& hash : referenced) {
        if (!ok) break;
        const auto blob = std::find_if(blobs.begin(), blobs.end(), [&](const AssetBlob& b) { return b.hash == hash; });
        const agent::Asset* asset = blob == blobs.end() ? nullptr : &blob->asset;
        if (asset == nullptr) {
            // Defensive; a referenced hash should always exist in the repository.
            continue;
        }
        const std::string entryName = "assets/" + hash + "." + asset->ext;
        // Assets are already-compressed PNG/JPEG: store them, don't re-deflate.
        ok = mz_zip_writer_add_mem_ex_v2(&zip, entryName.c_str(), asset->bytes.data(), asset->bytes.size(), nullptr, 0,
                                         MZ_NO_COMPRESSION, 0, 0, &fixedTime, nullptr, 0, nullptr, 0);
    }

    void* heapBuf = nullptr;
    size_t heapSize = 0;
    if (ok) {
        ok = mz_zip_writer_finalize_heap_archive(&zip, &heapBuf, &heapSize);
    }

    QByteArray result;
    if (ok && heapBuf != nullptr) {
        result = QByteArray(static_cast<const char*>(heapBuf), static_cast<int>(heapSize));
    }
    if (heapBuf != nullptr) {
        mz_free(heapBuf);  // allocated by miniz's own heap allocator (default: malloc/realloc/free)
    }
    mz_zip_writer_end(&zip);

    return ok ? result : jsonBytes;  // defensive fallback
}

OpenedContainer openContainer(const QByteArray& bytes) {
    OpenedContainer result;

    mz_zip_archive zip;
    mz_zip_zero_struct(&zip);
    if (!mz_zip_reader_init_mem(&zip, bytes.constData(), static_cast<size_t>(bytes.size()), 0)) {
        result.error = QStringLiteral("corrupt or unreadable ZIP archive");
        return result;  // ok stays false
    }

    bool sawModelJson = false;
    bool structOk = true;
    QString structError;

    const mz_uint numFiles = mz_zip_reader_get_num_files(&zip);
    for (mz_uint i = 0; i < numFiles; ++i) {
        mz_zip_archive_file_stat stat;
        if (!mz_zip_reader_file_stat(&zip, i, &stat)) {
            structOk = false;
            structError = QStringLiteral("ZIP archive: failed to stat entry %1").arg(i);
            break;
        }
        if (stat.m_is_directory) {
            continue;
        }
        const QString name = QString::fromUtf8(stat.m_filename);

        size_t extractedSize = 0;
        void* heapBuf = mz_zip_reader_extract_to_heap(&zip, i, &extractedSize, 0);
        if (heapBuf == nullptr) {
            structOk = false;
            structError = QStringLiteral("ZIP archive: failed to extract entry \"") + name + QStringLiteral("\"");
            break;
        }
        QByteArray entryBytes(static_cast<const char*>(heapBuf), static_cast<int>(extractedSize));
        mz_free(heapBuf);

        if (name == QStringLiteral("model.json")) {
            result.jsonBytes = entryBytes;
            sawModelJson = true;
            continue;
        }

        if (name.startsWith(QStringLiteral("assets/"))) {
            const QString rest = name.mid(7);  // strip "assets/"
            const int dot = rest.lastIndexOf(QLatin1Char('.'));
            if (dot <= 0) {
                structOk = false;
                structError = QStringLiteral("ZIP archive: malformed asset entry name \"") + name + QStringLiteral("\"");
                break;
            }
            const std::string hash = rest.left(dot).toStdString();
            const std::string ext = rest.mid(dot + 1).toStdString();
            if (result.assets.count(hash) != 0) {
                structOk = false;
                structError =
                    QStringLiteral("ZIP archive: duplicate asset hash \"") + QString::fromStdString(hash) + QStringLiteral("\"");
                break;
            }
            result.assets.emplace(hash, agent::Asset{entryBytes.toStdString(), ext});
            continue;
        }

        // Other top-level entries are ignored.
    }

    mz_zip_reader_end(&zip);

    if (!structOk) {
        OpenedContainer failed;
        failed.error = structError;
        return failed;
    }
    if (!sawModelJson) {
        OpenedContainer failed;
        failed.error = QStringLiteral("ZIP archive: missing required \"model.json\" entry");
        return failed;
    }

    result.ok = true;
    return result;
}

}  // namespace plnr::io
