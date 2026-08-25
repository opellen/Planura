#include "agent/sha256.h"

#include <cstddef>
#include <cstring>

// See sha256.h's own top comment: a direct, from-spec (FIPS 180-4)
// implementation, kept intentionally minimal.
namespace plnr::agent {

namespace {

constexpr std::array<std::uint32_t, 64> kRoundConstants = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

std::uint32_t rotr(std::uint32_t x, unsigned n) {
    return (x >> n) | (x << (32 - n));
}

// Big-endian byte-swap helper -- SHA-256 defines all multi-byte quantities
// (message words, length suffix, digest output) as big-endian regardless of
// host endianness.
std::uint32_t loadBigEndian32(const std::uint8_t* p) {
    return (static_cast<std::uint32_t>(p[0]) << 24) | (static_cast<std::uint32_t>(p[1]) << 16) |
           (static_cast<std::uint32_t>(p[2]) << 8) | static_cast<std::uint32_t>(p[3]);
}

void storeBigEndian32(std::uint32_t v, std::uint8_t* p) {
    p[0] = static_cast<std::uint8_t>(v >> 24);
    p[1] = static_cast<std::uint8_t>(v >> 16);
    p[2] = static_cast<std::uint8_t>(v >> 8);
    p[3] = static_cast<std::uint8_t>(v);
}

// Processes exactly one 64-byte block, updating state in place (FIPS 180-4
// §6.2.2's message schedule + compression function).
void processBlock(std::array<std::uint32_t, 8>& state, const std::uint8_t block[64]) {
    std::array<std::uint32_t, 64> w{};
    for (int i = 0; i < 16; ++i) {
        w[static_cast<std::size_t>(i)] = loadBigEndian32(block + i * 4);
    }
    for (int i = 16; i < 64; ++i) {
        const std::uint32_t s0 = rotr(w[static_cast<std::size_t>(i - 15)], 7) ^ rotr(w[static_cast<std::size_t>(i - 15)], 18) ^
                                  (w[static_cast<std::size_t>(i - 15)] >> 3);
        const std::uint32_t s1 = rotr(w[static_cast<std::size_t>(i - 2)], 17) ^ rotr(w[static_cast<std::size_t>(i - 2)], 19) ^
                                  (w[static_cast<std::size_t>(i - 2)] >> 10);
        w[static_cast<std::size_t>(i)] = w[static_cast<std::size_t>(i - 16)] + s0 + w[static_cast<std::size_t>(i - 7)] + s1;
    }

    std::uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
    std::uint32_t e = state[4], f = state[5], g = state[6], h = state[7];

    for (int i = 0; i < 64; ++i) {
        const std::uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        const std::uint32_t ch = (e & f) ^ (~e & g);
        const std::uint32_t temp1 = h + s1 + ch + kRoundConstants[static_cast<std::size_t>(i)] + w[static_cast<std::size_t>(i)];
        const std::uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        const std::uint32_t temp2 = s0 + maj;

        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }

    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
    state[5] += f;
    state[6] += g;
    state[7] += h;
}

}  // namespace

std::array<std::uint8_t, 32> sha256(std::string_view data) {
    // FIPS 180-4 §5.1.1: initial hash values (first 32 bits of the
    // fractional parts of the square roots of the first 8 primes).
    std::array<std::uint32_t, 8> state = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                           0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};

    const auto* bytes = reinterpret_cast<const std::uint8_t*>(data.data());
    const std::size_t len = data.size();

    // Whole 64-byte blocks first.
    std::size_t fullBlocks = len / 64;
    for (std::size_t i = 0; i < fullBlocks; ++i) {
        processBlock(state, bytes + i * 64);
    }
    std::size_t remaining = len - fullBlocks * 64;

    // Final padded block(s): the remaining tail bytes, a single 0x80 bit,
    // zero padding, and the 64-bit big-endian bit length -- spanning a
    // second block if the tail plus the 0x80/length suffix doesn't fit in
    // one.
    std::uint8_t tail[128] = {0};
    std::memcpy(tail, bytes + fullBlocks * 64, remaining);
    tail[remaining] = 0x80;
    const std::size_t withPad = remaining + 1;
    const std::size_t blockCount = withPad <= 56 ? 1 : 2;
    const std::uint64_t bitLen = static_cast<std::uint64_t>(len) * 8;
    for (int i = 0; i < 8; ++i) {
        tail[blockCount * 64 - 8 + i] = static_cast<std::uint8_t>(bitLen >> (56 - 8 * i));
    }
    for (std::size_t i = 0; i < blockCount; ++i) {
        processBlock(state, tail + i * 64);
    }

    std::array<std::uint8_t, 32> digest{};
    for (int i = 0; i < 8; ++i) {
        storeBigEndian32(state[static_cast<std::size_t>(i)], digest.data() + i * 4);
    }
    return digest;
}

std::string sha256Hex(std::string_view data) {
    static constexpr char kHexDigits[] = "0123456789abcdef";
    const std::array<std::uint8_t, 32> digest = sha256(data);
    std::string hex;
    hex.reserve(64);
    for (std::uint8_t byte : digest) {
        hex.push_back(kHexDigits[byte >> 4]);
        hex.push_back(kHexDigits[byte & 0x0f]);
    }
    return hex;
}

}  // namespace plnr::agent
