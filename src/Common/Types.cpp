#include "Types.h"

#include <stdexcept>

auto validateHashConfig(const HashConfig &config) -> bool {
    constexpr HashConfig approved;
    return config.profileId == approved.profileId && config.hashKind == approved.hashKind
        && config.stateBits == approved.stateBits && config.gearTableRevision == approved.gearTableRevision
        && config.initialValue == approved.initialValue && config.updateBeforeSample
        && !config.resetAtSubchunk && config.featureEncoding == approved.featureEncoding
        && config.superFeatureHashProfile == approved.superFeatureHashProfile
        && config.subchunkPolicy == approved.subchunkPolicy && config.shortChunkPolicy == approved.shortChunkPolicy;
}

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
auto subchunkBoundary(const std::size_t length, const std::size_t index) -> std::size_t {
    if (index > FEATURE_COUNT) {
        throw std::out_of_range("subchunk index exceeds FEATURE_COUNT");
    }
    return (length / FEATURE_COUNT) * index + ((length % FEATURE_COUNT) * index) / FEATURE_COUNT;
}
