#pragma once

#include "Common/Types.h"

//按组内排名将 12 个 features 组合成 3 个 SF 输入组
//输入：features：按子块顺序排列的 12 个值。
//输出：返回 3×4 的分组；先对四组各三个值稳定降序排序，再按相同排名跨组取值，原数组不变。
auto groupFinesseFeatures(const FeatureValues &features) -> FeatureGroups;
//提取 chunk 的 Finesse features 和 SF
//输入：chunk：只读字节，调用期间保持有效；config：已批准的 Gear 配置。
//输出：返回状态、12 个子块最大指纹和 3 个 SF；非法输入或短块通过状态表示。
auto extractFinesse(ChunkView chunk, const HashConfig &config) -> SignatureResult;
