#include "Types.h"

#include <stdexcept>

//检查哈希配置是否与已批准的 Gear 配置一致
//输入：config：待检查的配置。
//输出：返回是否完全符合冻结配置，不修改 config。
auto validateHashConfig(const HashConfig &config) -> bool {
    constexpr HashConfig approved;
    return config.profileId == approved.profileId && config.hashKind == approved.hashKind
        && config.stateBits == approved.stateBits && config.gearTableRevision == approved.gearTableRevision
        && config.initialValue == approved.initialValue && config.updateBeforeSample
        && !config.resetAtSubchunk && config.featureEncoding == approved.featureEncoding
        && config.superFeatureHashProfile == approved.superFeatureHashProfile
        && config.subchunkPolicy == approved.subchunkPolicy && config.shortChunkPolicy == approved.shortChunkPolicy;
}

//检查 chunk 是否可以进入 SF 提取
//输入：chunk：只读字节视图；config：哈希配置。
//输出：返回输入无效、配置无效、长度不支持或成功的状态，不计算签名。
auto validateChunk(const ChunkView chunk, const HashConfig &config) -> ExtractStatus {
    if (chunk.length > 0 && chunk.data == nullptr) {
        return ExtractStatus::InvalidInput;
    }
    if (!validateHashConfig(config)) {
        return ExtractStatus::InvalidConfig;
    }
    return chunk.length < FEATURE_COUNT ? ExtractStatus::UnsupportedLength : ExtractStatus::Ok;
}

//返回chunk中第index个的边界所在下标
//计算 chunk 均分成 12 段时的第 index 个边界
//输入：length：chunk 字节数；index：边界序号 0～12。
//输出：返回 floor(index*length/12) 的字节下标；序号越界时抛异常。
auto subchunkBoundary(const std::size_t length, const std::size_t index) -> std::size_t {
    if (index > FEATURE_COUNT) {
        throw std::out_of_range("subchunk index exceeds FEATURE_COUNT");
    }
    return (length / FEATURE_COUNT) * index + ((length % FEATURE_COUNT) * index) / FEATURE_COUNT;
}
