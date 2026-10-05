#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

// Qt-free SHA-256 (FIPS 180-4): plnr_agent must not link Qt, so no QCryptographicHash for AssetRepository's
// content-hash naming. One-shot digest only, no streaming.
namespace plnr::agent {

// Full 32-byte SHA-256 digest of data.
std::array<std::uint8_t, 32> sha256(std::string_view data);

// sha256(data) as a lowercase 64-char hex string.
std::string sha256Hex(std::string_view data);

}  // namespace plnr::agent
