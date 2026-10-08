#include "fastdb/keygen.hpp"
#include <random>

namespace fastdb {

std::string generate_key(size_t length) {
    static const char characters[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    static const size_t char_count = sizeof(characters) - 1;

    static thread_local std::random_device rd;
    static thread_local std::mt19937 gen(rd());
    std::uniform_int_distribution<size_t> dis(0, char_count - 1);

    std::string key;
    key.reserve(length);
    for (size_t i = 0; i < length; ++i) {
        key.push_back(characters[dis(gen)]);
    }
    return key;
}

} // namespace fastdb
