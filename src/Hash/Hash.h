#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

using Sha1Digest = std::array<std::uint8_t, 20>;

// 对完整内容求摘要；摘要命中后仍须逐字节确认，不能仅凭 SHA-1 合并块。
auto sha1(std::string_view data) -> Sha1Digest;
auto sha1Hex(const Sha1Digest &digest) -> std::string;

struct Sha1DigestHash {
    auto operator()(const Sha1Digest &digest) const noexcept -> std::size_t;
};
