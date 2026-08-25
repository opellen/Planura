#include "io/plr_container.h"

#include <set>
#include <utility>

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>

#include <miniz.h>

// Uses miniz's heap-backed writer/reader APIs throughout -- everything stays
// in memory (no temp-file archive on disk), matching Save/
// OpenDocumentCommand's own "bytes in, bytes out" shape.
namespace plnr::io {

namespace {

// Fixed epoch timestamp for every ZIP entry (not miniz's default "now") --
// determinism: same agent state -> byte-identical ZIP on every call.
MZ_TIME_T fixedEntryTime() {
    return static_cast<MZ_TIME_T>(0);
}

// Every DISTINCT, non-empty texture.assetHash referenced by doc's
// "materials" array, sorted ascending. Best-effort scan for the raw-vs-zip
// decision, not a validator -- malformed input is simply skipped (real
// validation is plr_reader.cpp's own strict parseMaterials, read-side).
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

QByteArray assembleContainer(const QJsonDocument& doc, const agent::AssetRepository& assetStore) {
    const QByteArray jsonBytes = doc.toJson(QJsonDocument::Indented);

    const std::vector<std::string> referenced = referencedAssetHashes(doc);
    if (referenced.empty()) {
        return jsonBytes;  // no assets -- byte-identical raw-JSON contract
    }

    mz_zip_archive zip;
    mz_zip_zero_struct(&zip);
    if (!mz_zip_writer_init_heap(&zip, 0, static_cast<size_t>(jsonBytes.size()) + 4096)) {
        // Defensive only -- init_heap fails only on allocation failure; no
        // error channel exists here, so falling back to raw bytes beats
        // crashing (SaveDocumentCommand's QSaveFile surfaces real I/O errors).
        return jsonBytes;
    }

    MZ_TIME_T fixedTime = fixedEntryTime();
    bool ok = mz_zip_writer_add_mem_ex_v2(&zip, "model.json", jsonBytes.constData(),
                                          static_cast<size_t>(jsonBytes.size()), nullptr, 0, MZ_DEFAULT_COMPRESSION, 0,
                                          0, &fixedTime, nullptr, 0, nullptr, 0);

    for (const std::string& hash : referenced) {
        if (!ok) break;
        const agent::Asset* asset = assetStore.get(hash);
        if (asset == nullptr) {
            // Defensive skip -- a referenced hash always came from a real
            // AssetRepository::add call when writing live state; should never fire.
            continue;
        }
        const std::string entryName = "assets/" + hash + "." + asset->ext;
        // MZ_NO_COMPRESSION for asset payloads: PNG/JPEG bytes are already
        // compressed, so re-deflating spends CPU for no size benefit;
        // model.json above keeps MZ_DEFAULT_COMPRESSION where deflate helps.
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

    return ok ? result : jsonBytes;  // defensive fallback -- see init_heap's own comment above
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

        // Any other top-level entry (reserved for a future thumbnail) ignored.
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
