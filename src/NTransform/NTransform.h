#pragma once

#include "Common/Types.h"

//取得已批准的 12 对 N-transform 乘加参数
//输入：无，参数来自代码中冻结的 D-15 表。
//输出：返回拥有固定乘数和加数的参数结构，不在运行时随机生成。
auto approvedTransformParameters() -> TransformParameters;
//检查 N-transform 参数是否与冻结表完全一致
//输入：parameters：待检查的 12 对乘数和加数。
//输出：返回是否全部一致，不修改参数。
auto validateTransformParameters(const TransformParameters &parameters) -> bool;
//对 Gear 指纹执行一次模 2^32 的线性变换
//输入：fingerprint：Gear 指纹，使用低 32 位；multiplier/addend：本维度的乘数和加数。
//输出：返回 (multiplier*low32(fingerprint)+addend) 的低 32 位。
auto transformFingerprint(std::uint64_t fingerprint,
                          std::uint32_t multiplier,
                          std::uint32_t addend) -> std::uint32_t;
//将一维 features 按连续四个一组整理成二维 groups
//输入：features：按变换维度顺序排列的 12 个最大值。
//输出：返回 3×4 的分组，不执行线性变换，不修改原数组。
auto groupNTransformFeatures(const FeatureValues &features) -> FeatureGroups;
//逐字节执行 12 维变换并提取 N-transform 签名
//输入：chunk：只读字节；config：Gear 配置；parameters：冻结乘加参数，视图在调用期间有效。
//输出：返回状态、12 个变换最大值和 3 个 SF；非法配置、输入或短块通过状态表示。
auto extractNTransform(ChunkView chunk, const HashConfig &config,
                       const TransformParameters &parameters) -> SignatureResult;
