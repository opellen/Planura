#include "agent/asset_repository.h"

#include <utility>

#include "agent/sha256.h"

namespace plnr::agent {

namespace {

std::string hashOf(const std::string& bytes) {
    return sha256Hex(bytes).substr(0, kAssetHashHexLength);
}

}  // namespace

AssetRepository::AssetRepository() : Agent(std::string(kAssetRepositoryName)) {}

std::string AssetRepository::add(std::string bytes, std::string ext) {
    const std::string hash = hashOf(bytes);
    auto it = assets_.find(hash);
    if (it != assets_.end()) {
        return hash;  // dedup -- existing content wins, ext not updated
    }
    assets_.emplace(hash, Asset{std::move(bytes), std::move(ext)});
    return hash;
}

const Asset* AssetRepository::get(std::string_view hash) const {
    auto it = assets_.find(std::string(hash));
    return it != assets_.end() ? &it->second : nullptr;
}

std::vector<std::string> AssetRepository::hashes() const {
    std::vector<std::string> out;
    out.reserve(assets_.size());
    for (const auto& [hash, asset] : assets_) {
        out.push_back(hash);
    }
    return out;
}

void AssetRepository::clearForRestore() {
    assets_.clear();
}

bool AssetRepository::restoreAsset(std::string hash, std::string bytes, std::string ext) {
    if (assets_.count(hash) != 0) {
        return false;  // duplicate hash in one file's assets list -- malformed input
    }
    if (hashOf(bytes) != hash) {
        return false;  // content-hash mismatch
    }
    assets_.emplace(std::move(hash), Asset{std::move(bytes), std::move(ext)});
    return true;
}

}  // namespace plnr::agent
