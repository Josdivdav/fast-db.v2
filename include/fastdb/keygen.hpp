#pragma once

#include <string>
#include <cstddef>

namespace fastdb {

/**
 * Generate a random alphanumeric key of given length (default 43).
 * Mirrors generateKey() from src/lib/keygen.js
 */
std::string generate_key(size_t length = 43);

} // namespace fastdb
