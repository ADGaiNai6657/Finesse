#pragma once

#include "Common/Types.h"

#include <string>
#include <string_view>

inline constexpr std::string_view XDELTA_REVISION = "5d936d2c30f78729375c3ae070248a05bef27bd2";
inline constexpr std::string_view XDELTA_PROFILE = "xdelta3-level6-adler32-no-secondary-v1";

// base=B、target=A，返回拥有的 VCDIFF 字节；长度/完整性由恢复接口另核对。
auto encodeDelta(ChunkView base, ChunkView target) -> std::string;
// expectedLength 限定可接受输出，错误/损坏数据抛异常；调用方另核对原文摘要/全部字节。
auto decodeDelta(ChunkView base, std::string_view delta, std::size_t expectedLength) -> std::string;
