#include "agent/asset_repository.h"

#include <algorithm>
#include <string>

#include <gtest/gtest.h>

// Qt-free, links plnr_agent directly -- no kernel registration needed
// since AssetRepository never dispatches (see AssetRepository's own class comment;
// its Restore API follows the same no-notification convention as other Agents).
namespace {

using plnr::agent::Asset;
using plnr::agent::AssetRepository;

TEST(AssetRepositoryTest, AddReturnsStableHashForIdenticalContent) {
    AssetRepository agent;
    const std::string hash1 = agent.add("hello world", "png");
    const std::string hash2 = agent.add("hello world", "png");
    EXPECT_EQ(hash1, hash2);
    EXPECT_EQ(hash1.size(), plnr::agent::kAssetHashHexLength);
}

TEST(AssetRepositoryTest, AddDifferentContentProducesDifferentHashes) {
    AssetRepository agent;
    const std::string hashA = agent.add("content A", "png");
    const std::string hashB = agent.add("content B", "png");
    EXPECT_NE(hashA, hashB);
}

TEST(AssetRepositoryTest, AddDedupesAndKeepsFirstExtension) {
    AssetRepository agent;
    const std::string hash1 = agent.add("same bytes", "png");
    const std::string hash2 = agent.add("same bytes", "jpg");  // second caller claims a different ext
    EXPECT_EQ(hash1, hash2);
    EXPECT_EQ(agent.hashes().size(), 1u);

    const Asset* asset = agent.get(hash1);
    ASSERT_NE(asset, nullptr);
    EXPECT_EQ(asset->ext, "png");  // first write wins
    EXPECT_EQ(asset->bytes, "same bytes");
}

TEST(AssetRepositoryTest, GetUnknownHashReturnsNullptr) {
    AssetRepository agent;
    EXPECT_EQ(agent.get("deadbeefdeadbeef"), nullptr);
}

TEST(AssetRepositoryTest, HashesReflectsEveryDistinctAsset) {
    AssetRepository agent;
    const std::string hashA = agent.add("content A", "png");
    const std::string hashB = agent.add("content B", "jpg");
    const auto hashes = agent.hashes();
    EXPECT_EQ(hashes.size(), 2u);
    EXPECT_NE(std::find(hashes.begin(), hashes.end(), hashA), hashes.end());
    EXPECT_NE(std::find(hashes.begin(), hashes.end(), hashB), hashes.end());
}

TEST(AssetRepositoryTest, ClearForRestoreEmptiesEveryAsset) {
    AssetRepository agent;
    agent.add("content A", "png");
    agent.add("content B", "jpg");
    ASSERT_EQ(agent.hashes().size(), 2u);

    agent.clearForRestore();
    EXPECT_TRUE(agent.hashes().empty());
}

TEST(AssetRepositoryTest, RestoreAssetAcceptsMatchingHash) {
    AssetRepository live;
    const std::string hash = live.add("real content", "png");

    AssetRepository restored;
    EXPECT_TRUE(restored.restoreAsset(hash, "real content", "png"));
    const Asset* asset = restored.get(hash);
    ASSERT_NE(asset, nullptr);
    EXPECT_EQ(asset->bytes, "real content");
    EXPECT_EQ(asset->ext, "png");
}

// Rejects a caller-claimed hash that doesn't match sha256(bytes), with no
// mutation -- the mechanism io::readDocument's ZIP-branch cross-validation
// relies on for its "asset content doesn't match its own hash" check.
TEST(AssetRepositoryTest, RestoreAssetRejectsHashContentMismatch) {
    AssetRepository agent;
    EXPECT_FALSE(agent.restoreAsset("deadbeefdeadbeef", "arbitrary bytes that don't hash to this", "png"));
    EXPECT_TRUE(agent.hashes().empty());
}

TEST(AssetRepositoryTest, RestoreAssetRejectsDuplicateHash) {
    AssetRepository live;
    const std::string hash = live.add("real content", "png");

    AssetRepository agent;
    ASSERT_TRUE(agent.restoreAsset(hash, "real content", "png"));
    EXPECT_FALSE(agent.restoreAsset(hash, "real content", "png"));  // same record replayed twice
    EXPECT_EQ(agent.hashes().size(), 1u);
}

TEST(AssetRepositoryTest, EmptyBytesHashConsistently) {
    // Edge case: an empty blob is legal input (not that anything ever adds
    // one in practice) -- sha256 of the empty string is well-defined, and
    // add()/restoreAsset() must agree on it like any other content.
    AssetRepository agent;
    const std::string hash = agent.add("", "png");
    EXPECT_EQ(hash.size(), plnr::agent::kAssetHashHexLength);

    AssetRepository restored;
    EXPECT_TRUE(restored.restoreAsset(hash, "", "png"));
}

}  // namespace
