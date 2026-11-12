#pragma once

#include "Common/Types.h"

#include <string>
#include <string_view>

inline constexpr std::string_view XDELTA_REVISION = "5d936d2c30f78729375c3ae070248a05bef27bd2";
inline constexpr std::string_view XDELTA_PROFILE = "xdelta3-level6-adler32-no-secondary-v1";

// base=B、target=A，返回拥有的 VCDIFF 字节；长度/完整性由恢复接口另核对。
//以基块 B 为来源，将目标 A 编码成真正的 VCDIFF
//输入：base：B 的只读视图；target：A 的只读视图，均在调用期间有效。
//输出：返回拥有 Delta 字节的 string；固定 level6、Adler32、无二级压缩，非法输入或编码失败抛异常。
auto encodeDelta(ChunkView base, ChunkView target) -> std::string;
// expectedLength 限定可接受输出，错误/损坏数据抛异常；调用方另核对原文摘要/全部字节。
//使用基块 B 和 VCDIFF 恢复目标 A
//输入：base：B 的只读视图；delta：VCDIFF 字节；expectedLength：必须恢复出的长度。
//输出：返回拥有恢复字节的 string；长度不符、Adler32 校验或解码失败时抛异常，调用者另核对目标内容。
auto decodeDelta(ChunkView base, std::string_view delta, std::size_t expectedLength) -> std::string;
