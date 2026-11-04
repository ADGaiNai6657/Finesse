#include "Hash.h"

#include <openssl/evp.h>

#include <stdexcept>

auto sha1(const std::string_view data) -> Sha1Digest {
    Sha1Digest digest {};
    unsigned int length = 0;
    // 沿用 FastCDC 的 OpenSSL EVP/SHA-1 语义，检查返回值；不保留线程局部裸指针。
    if (EVP_Digest(data.data(), data.size(), digest.data(), &length, EVP_sha1(), nullptr) != 1
        || length != digest.size()) {
        throw std::runtime_error("OpenSSL SHA-1 failed");
    }
    return digest;
}

auto sha1Hex(const Sha1Digest &digest) -> std::string {
    constexpr char HEX[] = "0123456789abcdef";
    std::string text;
    text.reserve(digest.size() * 2);
    for (const auto byte : digest) {
        text += HEX[byte >> 4];
        text += HEX[byte & 15];
    }
    return text;
}

auto Sha1DigestHash::operator()(const Sha1Digest &digest) const noexcept -> std::size_t {
    // 容器桶号只影响查找性能；unordered_multimap 仍比较完整 20 字节键。
    std::size_t hash = static_cast<std::size_t>(14695981039346656037ULL);
    for (const auto byte : digest) {
        hash = (hash ^ byte) * static_cast<std::size_t>(1099511628211ULL);
    }
    return hash;
}
