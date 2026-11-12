#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

inline constexpr std::size_t SUPER_FEATURE_COUNT = 3;
inline constexpr std::size_t FEATURES_PER_SUPER_FEATURE = 4;
inline constexpr std::size_t FEATURE_COUNT = SUPER_FEATURE_COUNT * FEATURES_PER_SUPER_FEATURE;
inline constexpr std::size_t FEATURE_BYTES = sizeof(std::uint64_t);
inline constexpr std::string_view PROFILE_ID = "finesse-gear-v1";
inline constexpr std::string_view UPSTREAM_REVISION = "be5816e8e2e8548738db3e7dd54aa323056df402";

using FingerprintValue = std::uint64_t;
using FeatureValues = std::array<FingerprintValue, FEATURE_COUNT>;
using SuperFeatures = std::array<FingerprintValue, SUPER_FEATURE_COUNT>;
using FeatureGroup = std::array<FingerprintValue, FEATURES_PER_SUPER_FEATURE>;
using FeatureGroups = std::array<FeatureGroup, SUPER_FEATURE_COUNT>;
using ChunkId = std::uint64_t;

// 只读借用；调用方须保持 data 在整个调用期间有效。
struct ChunkView {
    const std::uint8_t *data = nullptr;
    std::size_t length = 0;
};

enum class ExtractStatus { Ok, InvalidInput, InvalidConfig, UnsupportedLength };

struct SignatureResult {
    ExtractStatus status = ExtractStatus::InvalidInput;
    FeatureValues features {};
    SuperFeatures superFeatures {};
};

struct TransformParameters {
    std::array<std::uint32_t, FEATURE_COUNT> multipliers {};
    std::array<std::uint32_t, FEATURE_COUNT> addends {};
};

// D-14 冻结的配置。所有字段在入口整体校验，逐字节热路径不解析配置。
struct HashConfig {
    std::string_view profileId = PROFILE_ID;
    std::string_view hashKind = "Gear";
    std::size_t stateBits = 64;
    std::string_view gearTableRevision = UPSTREAM_REVISION;
    std::uint64_t initialValue = 0;
    bool updateBeforeSample = true;
    bool resetAtSubchunk = false;
    std::string_view featureEncoding = "uint64-le-4x8";
    std::string_view superFeatureHashProfile = "gear64-zero-final";
    std::string_view subchunkPolicy = "floor-full-coverage";
    std::string_view shortChunkPolicy = "exclude-length-lt-12";
};

//检查哈希配置是否与已批准的 Gear 配置一致
//输入：config：待检查的配置。
//输出：返回是否完全符合冻结配置，不修改 config。
auto validateHashConfig(const HashConfig &config) -> bool;
//检查 chunk 是否可以进入 SF 提取
//输入：chunk：只读字节视图；config：哈希配置。
//输出：返回输入无效、配置无效、长度不支持或成功的状态，不计算签名。
auto validateChunk(ChunkView chunk, const HashConfig &config) -> ExtractStatus;

// floor(index*length/N)，以商和余数计算，避免 index*length 溢出。
//计算 chunk 均分成 12 段时的第 index 个边界
//输入：length：chunk 字节数；index：边界序号 0～12。
//输出：返回 floor(index*length/12) 的字节下标；序号越界时抛异常。
auto subchunkBoundary(std::size_t length, std::size_t index) -> std::size_t;
