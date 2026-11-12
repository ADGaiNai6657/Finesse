#pragma once

#include "Common/Types.h"

using EncodedFeatureGroup = std::array<std::uint8_t, FEATURES_PER_SUPER_FEATURE * FEATURE_BYTES>;

//把一个 SF 的四个 features 处理成固定字节串
//输入：features：按组合顺序排列的四个 uint64 值。
//输出：返回 32 字节数组，每个值按 8 字节小端编码，不依赖主机端序。
auto serializeFeatures(const FeatureGroup &features) -> EncodedFeatureGroup;
//将各组 features 编码并计算对应的 SF
//输入：groups：三个有序分组，每组四个 feature。
//输出：返回 3 个 SF；每组先编码成字节，再从零 Gear 状态滚入取最终值。
auto hashSuperFeatures(const FeatureGroups &groups) -> SuperFeatures;
