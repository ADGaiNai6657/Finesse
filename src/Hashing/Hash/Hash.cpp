#include "Hash.h"

#include <openssl/evp.h>

#include <stdexcept>

//计算完整 chunk 内容的 SHA-1 摘要
//输入：data：只读字节串，可含零字节，调用期间保持有效。
//输出：返回完整 20 字节摘要；OpenSSL 失败时抛异常，摘要命中后的字节确认由 Store 完成。
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

//把 SHA-1 摘要转换成便于记录的文本
//输入：digest：完整 20 字节 SHA-1 摘要。
//输出：返回 40 个小写十六进制字符，不修改摘要。
auto sha1Hex(const Sha1Digest &digest) -> std::string {
    constexpr char HEX[] = "0123456789abcdef";
    std::string text;
    text.reserve(digest.size() * 2);    //n个byte,则预留 2*n 个位置    1 byte -> 2 char
    for (const auto byte : digest) {    //按byte遍历,byte 位移和按位与15则分别取高位4位与低位4位,并通过查表转化为char存储
        text += HEX[byte >> 4];
        text += HEX[byte & 15];
    }
    return text;
}

//将完整 SHA-1 摘要混合成容器桶号
//输入：digest：20 字节摘要键。
//输出：返回 size_t 桶哈希值；只用于容器查找，不能代替完整摘要或字节相等判断。
auto Sha1DigestHash::operator()(const Sha1Digest &digest) const noexcept -> std::size_t {
    // 容器桶号只影响查找性能；unordered_multimap 仍比较完整 20 字节键。
    std::size_t hash = static_cast<std::size_t>(14695981039346656037ULL);
    for (const auto byte : digest) {
        hash = (hash ^ byte) * static_cast<std::size_t>(1099511628211ULL);
    }
    return hash;
}
