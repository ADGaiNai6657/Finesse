#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

using Sha1Digest = std::array<std::uint8_t, 20>;

// 对完整内容求摘要；摘要命中后仍须逐字节确认，不能仅凭 SHA-1 合并块。
//计算完整 chunk 内容的 SHA-1 摘要
//输入：data：只读字节串，可含零字节，调用期间保持有效。
//输出：返回完整 20 字节摘要；OpenSSL 失败时抛异常，摘要命中后的字节确认由 Store 完成。
auto sha1(std::string_view data) -> Sha1Digest;
//把 SHA-1 摘要转换成便于记录的文本
//输入：digest：完整 20 字节 SHA-1 摘要。
//输出：返回 40 个小写十六进制字符，不修改摘要。
auto sha1Hex(const Sha1Digest &digest) -> std::string;

struct Sha1DigestHash {
    //将完整 SHA-1 摘要混合成容器桶号
    //输入：digest：20 字节摘要键。
    //输出：返回 size_t 桶哈希值；只用于容器查找，不能代替完整摘要或字节相等判断。
    auto operator()(const Sha1Digest &digest) const noexcept -> std::size_t;
};
