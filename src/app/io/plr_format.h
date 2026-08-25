#pragma once

#include <string_view>

#include <QByteArray>

namespace plnr::io {

// The .plr format's identity constants, checked by the reader before
// trusting anything else. formatVersion bumps only on a breaking schema change.
inline constexpr std::string_view kFormatName = "planura";
inline constexpr int kFormatVersion = 1;

// Which container a .plr file's bytes are wrapped in. v1 always writes
// RawJson; Zip is reserved for a future compressed variant.
enum class Container { RawJson, Zip, Unknown };

// Identifies head's container from its leading bytes only. Zip: "PK\x03\x04"
// at offset 0. RawJson: after skipping leading ASCII whitespace, next byte
// is '{'. Anything else -> Unknown.
inline Container sniffContainer(const QByteArray& head) {
    if (head.size() >= 4 && head[0] == 'P' && head[1] == 'K' && head[2] == '\x03' && head[3] == '\x04') {
        return Container::Zip;
    }
    for (const char ch : head) {
        if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') {
            continue;
        }
        return ch == '{' ? Container::RawJson : Container::Unknown;
    }
    return Container::Unknown;  // empty, or all leading whitespace with nothing after
}

}  // namespace plnr::io
