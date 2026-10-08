#include "fastdb/crypto.hpp"
#include <random>
#include <sstream>
#include <iomanip>
#include <cstring>
#include <stdexcept>

namespace fastdb {
namespace crypto {

// ---------------------------------------------------------------------------
// RFC 4122 UUID v4
// ---------------------------------------------------------------------------
std::string random_uuid() {
    static thread_local std::random_device rd;
    static thread_local std::mt19937_64 gen(rd());
    static thread_local std::uniform_int_distribution<uint64_t> dis;

    uint64_t ab = dis(gen);
    uint64_t cd = dis(gen);

    // Set version 4 (bits 12-15 of time_hi_and_version)
    ab = (ab & 0xFFFFFFFFFFFF0FFFULL) | 0x0000000000004000ULL;
    // Set variant 1 (bits 6-7 of clock_seq_hi_and_reserved)
    cd = (cd & 0x3FFFFFFFFFFFFFFFULL) | 0x8000000000000000ULL;

    char buf[37];
    std::snprintf(buf, sizeof(buf),
                  "%08x-%04x-%04x-%04x-%012llx",
                  static_cast<uint32_t>((ab >> 32) & 0xFFFFFFFF),
                  static_cast<uint16_t>((ab >> 16) & 0xFFFF),
                  static_cast<uint16_t>(ab & 0xFFFF),
                  static_cast<uint16_t>((cd >> 48) & 0xFFFF),
                  static_cast<unsigned long long>(cd & 0xFFFFFFFFFFFFULL));
    return std::string(buf);
}

// ---------------------------------------------------------------------------
// Base64 and Base64URL
// ---------------------------------------------------------------------------
static const char B64_CHARS[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string base64_encode(const uint8_t* data, size_t length) {
    std::string ret;
    ret.reserve(((length + 2) / 3) * 4);
    int val = 0, valb = -6;
    for (size_t i = 0; i < length; ++i) {
        val = (val << 8) + data[i];
        valb += 8;
        while (valb >= 0) {
            ret.push_back(B64_CHARS[(val >> valb) & 0x3F]);
            valb -= 6;
        }
    }
    if (valb > -6) {
        ret.push_back(B64_CHARS[((val << 8) >> (valb + 8)) & 0x3F]);
    }
    while (ret.size() % 4 != 0) {
        ret.push_back('=');
    }
    return ret;
}

std::string base64_encode(const std::string& data) {
    return base64_encode(reinterpret_cast<const uint8_t*>(data.data()), data.size());
}

std::string base64_decode(const std::string& input) {
    std::string out;
    std::vector<int> T(256, -1);
    for (int i = 0; i < 64; i++) T[static_cast<uint8_t>(B64_CHARS[i])] = i;

    int val = 0, valb = -8;
    for (uint8_t c : input) {
        if (T[c] == -1) break;
        val = (val << 6) + T[c];
        valb += 6;
        if (valb >= 0) {
            out.push_back(static_cast<char>((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    return out;
}

std::string base64url_encode(const uint8_t* data, size_t length) {
    std::string b64 = base64_encode(data, length);
    std::string b64url;
    b64url.reserve(b64.size());
    for (char c : b64) {
        if (c == '+') b64url.push_back('-');
        else if (c == '/') b64url.push_back('_');
        else if (c == '=') { /* strip padding */ }
        else b64url.push_back(c);
    }
    return b64url;
}

std::string base64url_encode(const std::string& data) {
    return base64url_encode(reinterpret_cast<const uint8_t*>(data.data()), data.size());
}

std::string base64url_decode(const std::string& input) {
    std::string b64 = input;
    for (char& c : b64) {
        if (c == '-') c = '+';
        else if (c == '_') c = '/';
    }
    while (b64.size() % 4 != 0) {
        b64.push_back('=');
    }
    return base64_decode(b64);
}

// ---------------------------------------------------------------------------
// Standard SHA-1 (RFC 3174) for WebSocket Handshake (RFC 6455)
// ---------------------------------------------------------------------------
std::vector<uint8_t> sha1_bytes(const uint8_t* data, size_t length) {
    uint32_t h[5] = {
        0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0
    };

    uint64_t total_bits = static_cast<uint64_t>(length) * 8;
    size_t padded_len = ((length + 9 + 63) / 64) * 64;
    std::vector<uint8_t> msg(padded_len, 0);
    std::memcpy(msg.data(), data, length);
    msg[length] = 0x80;

    for (int i = 0; i < 8; ++i) {
        msg[padded_len - 1 - i] = static_cast<uint8_t>((total_bits >> (i * 8)) & 0xFF);
    }

    auto rol = [](uint32_t val, uint32_t shift) -> uint32_t {
        return (val << shift) | (val >> (32 - shift));
    };

    for (size_t chunk = 0; chunk < padded_len; chunk += 64) {
        uint32_t w[80];
        for (int i = 0; i < 16; ++i) {
            w[i] = (static_cast<uint32_t>(msg[chunk + i * 4]) << 24) |
                   (static_cast<uint32_t>(msg[chunk + i * 4 + 1]) << 16) |
                   (static_cast<uint32_t>(msg[chunk + i * 4 + 2]) << 8) |
                   (static_cast<uint32_t>(msg[chunk + i * 4 + 3]));
        }
        for (int i = 16; i < 80; ++i) {
            w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        }

        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];

        for (int i = 0; i < 80; ++i) {
            uint32_t f, k;
            if (i < 20) {
                f = (b & c) | ((~b) & d);
                k = 0x5A827999;
            } else if (i < 40) {
                f = b ^ c ^ d;
                k = 0x6ED9EBA1;
            } else if (i < 60) {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8F1BBCDC;
            } else {
                f = b ^ c ^ d;
                k = 0xCA62C1D6;
            }

            uint32_t temp = rol(a, 5) + f + e + k + w[i];
            e = d;
            d = c;
            c = rol(b, 30);
            b = a;
            a = temp;
        }

        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
    }

    std::vector<uint8_t> digest(20);
    for (int i = 0; i < 5; ++i) {
        digest[i * 4]     = static_cast<uint8_t>((h[i] >> 24) & 0xFF);
        digest[i * 4 + 1] = static_cast<uint8_t>((h[i] >> 16) & 0xFF);
        digest[i * 4 + 2] = static_cast<uint8_t>((h[i] >> 8) & 0xFF);
        digest[i * 4 + 3] = static_cast<uint8_t>(h[i] & 0xFF);
    }
    return digest;
}

std::vector<uint8_t> sha1_bytes(const std::string& data) {
    return sha1_bytes(reinterpret_cast<const uint8_t*>(data.data()), data.size());
}

std::string sha1_base64(const std::string& input) {
    auto bytes = sha1_bytes(input);
    return base64_encode(bytes.data(), bytes.size());
}

// ---------------------------------------------------------------------------
// Standard SHA-256 (FIPS 180-2)
// ---------------------------------------------------------------------------
namespace {

inline uint32_t ror(uint32_t val, uint32_t shift) {
    return (val >> shift) | (val << (32 - shift));
}

inline uint32_t ch(uint32_t x, uint32_t y, uint32_t z) { return (x & y) ^ (~x & z); }
inline uint32_t maj(uint32_t x, uint32_t y, uint32_t z) { return (x & y) ^ (x & z) ^ (y & z); }
inline uint32_t sigma0(uint32_t x) { return ror(x, 2) ^ ror(x, 13) ^ ror(x, 22); }
inline uint32_t sigma1(uint32_t x) { return ror(x, 6) ^ ror(x, 11) ^ ror(x, 25); }
inline uint32_t gamma0(uint32_t x) { return ror(x, 7) ^ ror(x, 18) ^ (x >> 3); }
inline uint32_t gamma1(uint32_t x) { return ror(x, 17) ^ ror(x, 19) ^ (x >> 10); }

static const uint32_t K256[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

} // anonymous namespace

std::vector<uint8_t> sha256_bytes(const uint8_t* data, size_t length) {
    uint32_t h[8] = {
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54feff4,
        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
    };

    uint64_t total_bits = static_cast<uint64_t>(length) * 8;
    size_t padded_len = ((length + 9 + 63) / 64) * 64;
    std::vector<uint8_t> msg(padded_len, 0);
    std::memcpy(msg.data(), data, length);
    msg[length] = 0x80;

    for (int i = 0; i < 8; ++i) {
        msg[padded_len - 1 - i] = static_cast<uint8_t>((total_bits >> (i * 8)) & 0xFF);
    }

    for (size_t chunk = 0; chunk < padded_len; chunk += 64) {
        uint32_t w[64];
        for (int i = 0; i < 16; ++i) {
            w[i] = (static_cast<uint32_t>(msg[chunk + i * 4]) << 24) |
                   (static_cast<uint32_t>(msg[chunk + i * 4 + 1]) << 16) |
                   (static_cast<uint32_t>(msg[chunk + i * 4 + 2]) << 8) |
                   (static_cast<uint32_t>(msg[chunk + i * 4 + 3]));
        }
        for (int i = 16; i < 64; ++i) {
            w[i] = gamma1(w[i - 2]) + w[i - 7] + gamma0(w[i - 15]) + w[i - 16];
        }

        uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
        uint32_t e = h[4], f = h[5], g = h[6], h_val = h[7];

        for (int i = 0; i < 64; ++i) {
            uint32_t t1 = h_val + sigma1(e) + ch(e, f, g) + K256[i] + w[i];
            uint32_t t2 = sigma0(a) + maj(a, b, c);
            h_val = g;
            g = f;
            f = e;
            e = d + t1;
            d = c;
            c = b;
            b = a;
            a = t1 + t2;
        }

        h[0] += a; h[1] += b; h[2] += c; h[3] += d;
        h[4] += e; h[5] += f; h[6] += g; h[7] += h_val;
    }

    std::vector<uint8_t> digest(32);
    for (int i = 0; i < 8; ++i) {
        digest[i * 4]     = static_cast<uint8_t>((h[i] >> 24) & 0xFF);
        digest[i * 4 + 1] = static_cast<uint8_t>((h[i] >> 16) & 0xFF);
        digest[i * 4 + 2] = static_cast<uint8_t>((h[i] >> 8) & 0xFF);
        digest[i * 4 + 3] = static_cast<uint8_t>(h[i] & 0xFF);
    }
    return digest;
}

std::vector<uint8_t> sha256_bytes(const std::string& data) {
    return sha256_bytes(reinterpret_cast<const uint8_t*>(data.data()), data.size());
}

std::string sha256_hex(const std::string& data) {
    auto bytes = sha256_bytes(data);
    std::ostringstream oss;
    for (uint8_t b : bytes) {
        oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(b);
    }
    return oss.str();
}

// ---------------------------------------------------------------------------
// HMAC-SHA-256 (RFC 2104)
// ---------------------------------------------------------------------------
std::vector<uint8_t> hmac_sha256_bytes(const std::string& key, const std::string& message) {
    const size_t BLOCK_SIZE = 64;
    std::vector<uint8_t> k(BLOCK_SIZE, 0);

    if (key.size() > BLOCK_SIZE) {
        auto key_hash = sha256_bytes(key);
        std::memcpy(k.data(), key_hash.data(), key_hash.size());
    } else {
        std::memcpy(k.data(), key.data(), key.size());
    }

    std::vector<uint8_t> o_key_pad(BLOCK_SIZE);
    std::vector<uint8_t> i_key_pad(BLOCK_SIZE);
    for (size_t i = 0; i < BLOCK_SIZE; ++i) {
        o_key_pad[i] = k[i] ^ 0x5c;
        i_key_pad[i] = k[i] ^ 0x36;
    }

    // Inner hash: sha256(i_key_pad || message)
    std::vector<uint8_t> inner_data = i_key_pad;
    inner_data.insert(inner_data.end(), message.begin(), message.end());
    auto inner_hash = sha256_bytes(inner_data.data(), inner_data.size());

    // Outer hash: sha256(o_key_pad || inner_hash)
    std::vector<uint8_t> outer_data = o_key_pad;
    outer_data.insert(outer_data.end(), inner_hash.begin(), inner_hash.end());
    return sha256_bytes(outer_data.data(), outer_data.size());
}

std::string hmac_sha256_hex(const std::string& key, const std::string& message) {
    auto bytes = hmac_sha256_bytes(key, message);
    std::ostringstream oss;
    for (uint8_t b : bytes) {
        oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(b);
    }
    return oss.str();
}

// ---------------------------------------------------------------------------
// JWT HS256 Implementation (matches jsonwebtoken npm package)
// ---------------------------------------------------------------------------
std::string jwt_sign(const nlohmann::json& payload, const std::string& secret, int64_t expiresInSeconds) {
    nlohmann::json header = {
        {"alg", "HS256"},
        {"typ", "JWT"}
    };

    nlohmann::json p = payload;
    int64_t now_sec = current_time_ms() / 1000;
    p["iat"] = now_sec;
    p["exp"] = now_sec + expiresInSeconds;

    std::string header_b64 = base64url_encode(header.dump());
    std::string payload_b64 = base64url_encode(p.dump());
    std::string msg = header_b64 + "." + payload_b64;

    auto sig_bytes = hmac_sha256_bytes(secret, msg);
    std::string sig_b64 = base64url_encode(sig_bytes.data(), sig_bytes.size());

    return msg + "." + sig_b64;
}

bool jwt_verify(const std::string& token, const std::string& secret, nlohmann::json& outPayload, std::string& outError) {
    size_t dot1 = token.find('.');
    if (dot1 == std::string::npos) {
        outError = "Invalid JWT token structure";
        return false;
    }
    size_t dot2 = token.find('.', dot1 + 1);
    if (dot2 == std::string::npos) {
        outError = "Invalid JWT token structure";
        return false;
    }

    std::string header_b64 = token.substr(0, dot1);
    std::string payload_b64 = token.substr(dot1 + 1, dot2 - dot1 - 1);
    std::string sig_b64 = token.substr(dot2 + 1);

    std::string msg = header_b64 + "." + payload_b64;
    auto expected_sig_bytes = hmac_sha256_bytes(secret, msg);
    std::string expected_sig_b64 = base64url_encode(expected_sig_bytes.data(), expected_sig_bytes.size());

    if (sig_b64 != expected_sig_b64) {
        outError = "Invalid token signature";
        return false;
    }

    try {
        std::string payload_json = base64url_decode(payload_b64);
        outPayload = nlohmann::json::parse(payload_json);
    } catch (const std::exception& e) {
        outError = std::string("Malformed JWT payload: ") + e.what();
        return false;
    }

    if (outPayload.contains("exp") && outPayload["exp"].is_number()) {
        int64_t exp = outPayload["exp"].get<int64_t>();
        int64_t now_sec = current_time_ms() / 1000;
        if (now_sec > exp) {
            outError = "Token expired";
            return false;
        }
    }

    return true;
}

} // namespace crypto
} // namespace fastdb
