#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include <QByteArray>
#include <QJsonDocument>
#include <QString>

#include "agent/asset_repository.h"

// The .plr container: wraps plr_writer's JSON into the bytes Save/Open handle (vendored miniz).
namespace plnr::io {

// No textured material -> doc.toJson(Indented) exactly (raw JSON). Otherwise a ZIP:
// "model.json" then "assets/<hash>.<ext>" per distinct hash, sorted ascending
// (entry order is part of the determinism contract); timestamps fixed at zero.
QByteArray assembleContainer(const QJsonDocument &doc,
                             const agent::AssetRepository &assetStore);

// One content-addressed asset copied out of an AssetRepository (value type, safe to hand to a worker).
struct AssetBlob {
  std::string hash;
  agent::Asset asset;
};

// Main-thread extractor: copies every asset in the repository.
std::vector<AssetBlob> collectAssetBlobs(const agent::AssetRepository &assetStore);

// Pure core of assembleContainer: touches no agent, so it may run on a worker. Byte-identical to
// the agent overload for blobs collected from the same repository (which delegates here).
QByteArray assembleContainer(const QJsonDocument &doc, const std::vector<AssetBlob> &blobs);

// Value snapshot for the async save path: everything the worker needs, nothing it must not touch.
struct SaveSnapshot {
  QJsonDocument doc;
  std::vector<AssetBlob> blobs;
};

// Value payload for the async open path (worker -> main): the parsed model plus the extracted
// container assets. fromContainer distinguishes a ZIP with zero assets from raw JSON, because
// plr_reader.cpp cross-validates material assetHash references only for ZIP input.
struct OpenPayload {
  QJsonDocument model;
  std::unordered_map<std::string, agent::Asset> assets;
  bool fromContainer{};
};

// Raw ZIP extraction result; assetHash cross-validation is plr_reader.cpp's job.
struct OpenedContainer {
  bool ok{};
  QString error;

  // "model.json" entry's raw bytes, ready for QJsonDocument::fromJson.
  QByteArray jsonBytes;

  // Every "assets/<hash>.<ext>" entry, keyed by parsed hash; the hash is not
  // verified against the bytes here (plr_reader.cpp does that).
  std::unordered_map<std::string, agent::Asset> assets;
};

// Parses bytes (already sniffed as Container::Zip). Extracts the required
// "model.json" and every "assets/<hash>.<ext>" entry; a missing/corrupt
// "model.json", unparseable asset name, or hash collision -> ok==false.
OpenedContainer openContainer(const QByteArray &bytes);

} // namespace plnr::io
