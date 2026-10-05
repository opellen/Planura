#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <ordo/core/agent.h>

namespace plnr::agent {

inline constexpr std::string_view kAssetRepositoryName = "assets";

// First kAssetHashHexLength hex chars of sha256Hex(bytes) -- long enough (64 bits) that a
// collision within one document's asset set isn't a practical concern.
inline constexpr std::size_t kAssetHashHexLength = 16;

// One immutable content-addressed blob: raw bytes plus the file extension (no leading dot).
// Extension is metadata only (hashing covers bytes alone); lets the .plr writer name "assets/<hash>.<ext>".
struct Asset {
    std::string bytes;  // raw binary content -- std::string is a fine binary-safe buffer, no text assumption
    std::string ext;
};

// Content-addressed immutable blob storage for material texture images. Deliberately NOT
// captured by Transaction's undo aux-diff (additive-only; undoing a texture-set reverts
// only the Material's own fields, leaving a harmless orphaned blob). No *Changed Fact of its own.
class AssetRepository : public ordo::core::Agent {
public:
    AssetRepository();

    // Registers bytes under content-hash naming, returning the hash. A repeat call with
    // already-known content dedups to the existing hash; ext is not re-checked on a dedup hit.
    std::string add(std::string bytes, std::string ext);

    // nullptr if hash names no known asset.
    const Asset* get(std::string_view hash) const;

    // Every known hash, in no particular order -- callers that need a
    // deterministic order (e.g. the .plr ZIP writer) sort it themselves.
    std::vector<std::string> hashes() const;

    // -- Restore API -----------------------------------------------------
    // File loader only: plain data manipulation, no notification (this agent never dispatches).

    // Empties every known asset.
    void clearForRestore();

    // Inserts hash -> {bytes, ext} verbatim, IF bytes actually hashes to hash (enforced here too,
    // not just add()). Returns false on a hash mismatch, or if hash is already present.
    bool restoreAsset(std::string hash, std::string bytes, std::string ext);

private:
    std::unordered_map<std::string, Asset> assets_;
};

}  // namespace plnr::agent
