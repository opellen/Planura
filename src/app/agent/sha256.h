#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

// A tiny, Qt-free SHA-256 implementation -- plnr_agent must never link Qt,
// so QCryptographicHash is off-limits for AssetRepository's content-hash naming.
// Implements FIPS 180-4 directly, hand-written from spec. Deliberately
// minimal: one algorithm, one one-shot digest entry point, no streaming.
namespace plnr::agent {

// Full 32-byte SHA-256 digest of data.
std::array<std::uint8_t, 32> sha256(std::string_view data);

// sha256(data)'s digest rendered as a lowercase 64-character hex string.
std::string sha256Hex(std::string_view data);

}  // namespace plnr::agent
