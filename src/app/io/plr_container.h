#pragma once

#include <string>
#include <unordered_map>

#include <QByteArray>
#include <QJsonDocument>
#include <QString>

#include "agent/asset_repository.h"

// The .plr ZIP container -- the seam between plr_writer.h's JSON and the
// bytes Save/Open actually read/write. Built on vendored miniz (third_party/miniz/).
namespace plnr::io {

// Assembles doc's bytes into the .plr container. No textured material ->
// doc.toJson(Indented) exactly (byte-identical to the pre-M11 raw-JSON
// contract). Otherwise a ZIP (miniz): "model.json" then "assets/<hash>.<ext>"
// per distinct hash, sorted ascending -- entry order is part of the
// determinism contract. Fixed zero timestamps (miniz otherwise stamps "now").
QByteArray assembleContainer(const QJsonDocument& doc, const agent::AssetRepository& assetStore);

// Raw ZIP extraction result -- not yet cross-validated against any
// Material's assetHash (plr_reader.cpp's job) or applied to any agent.
struct OpenedContainer {
    bool ok{};
    QString error;

    // "model.json" entry's raw bytes -- hand to QJsonDocument::fromJson
    // exactly like the raw-JSON path already does.
    QByteArray jsonBytes;

    // Every "assets/<hash>.<ext>" entry found, keyed by the parsed hash --
    // not yet verified to actually hash to that key (plr_reader.cpp's
    // all-or-nothing job, before anything is applied to an agent).
    std::unordered_map<std::string, agent::Asset> assets;
};

// Parses bytes (already sniffed as Container::Zip). Extracts the required
// "model.json" and every "assets/<hash>.<ext>" entry; a missing/corrupt
// "model.json", unparseable asset name, or hash collision -> ok==false.
OpenedContainer openContainer(const QByteArray& bytes);

}  // namespace plnr::io
