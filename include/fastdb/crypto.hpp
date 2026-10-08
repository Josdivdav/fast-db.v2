#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <chrono>
#include <nlohmann/json.hpp>

namespace fastdb {
namespace crypto {

// Returns current Unix timestamp in milliseconds
inline int64_t current_time_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

// Generate RFC 4122 compliant version 4 UUID string
std::string random_uuid();

// Base64 & Base64URL utilities
std::string base64_encode(const uint8_t* data, size_t length);
std::string base64_encode(const std::string& data);
std::string base64_decode(const std::string& input);

std::string base64url_encode(const uint8_t* data, size_t length);
std::string base64url_encode(const std::string& data);
std::string base64url_decode(const std::string& input);

// SHA-1 (RFC 3174) for WebSocket Handshake (RFC 6455)
std::vector<uint8_t> sha1_bytes(const uint8_t* data, size_t length);
std::vector<uint8_t> sha1_bytes(const std::string& data);
std::string sha1_base64(const std::string& input);

// SHA-256 and HMAC-SHA-256
std::vector<uint8_t> sha256_bytes(const uint8_t* data, size_t length);
std::vector<uint8_t> sha256_bytes(const std::string& data);
std::string sha256_hex(const std::string& data);

std::vector<uint8_t> hmac_sha256_bytes(const std::string& key, const std::string& message);
std::string hmac_sha256_hex(const std::string& key, const std::string& message);

// JWT HS256 utilities
std::string jwt_sign(const nlohmann::json& payload, const std::string& secret, int64_t expiresInSeconds = 7 * 24 * 3600);
bool jwt_verify(const std::string& token, const std::string& secret, nlohmann::json& outPayload, std::string& outError);

} // namespace crypto
} // namespace fastdb
