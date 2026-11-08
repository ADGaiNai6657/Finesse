#include "Finesse.h"

#include "GearHash/GearHash.h"
#include "SuperFeature/SuperFeature.h"

#include <algorithm>

auto groupFinesseFeatures(const FeatureValues &features) -> FeatureGroups {
    FeatureGroups groups {};
    for (std::size_t g = 0; g < FEATURES_PER_SUPER_FEATURE; ++g) {
        std::array<FingerprintValue, SUPER_FEATURE_COUNT> ranked {};
        std::copy_n(features.begin() + g * SUPER_FEATURE_COUNT, SUPER_FEATURE_COUNT, ranked.begin());
        // 三元素稳定插入排序；仅严格较大时移动，保持相等项来源顺序，且不分配堆内存。
        for (std::size_t i = 1; i < ranked.size(); ++i) {
            const auto value = ranked[i];
            std::size_t j = i;
            while (j > 0 && value > ranked[j - 1]) {    //排序
                ranked[j] = ranked[j - 1];
                --j;
            }
            ranked[j] = value;
        }
        for (std::size_t s = 0; s < SUPER_FEATURE_COUNT; ++s) {
            groups[s][g] = ranked[s];
        }
    }
    return groups;
}

auto extractFinesse(const ChunkView chunk, const HashConfig &config) -> SignatureResult {
    SignatureResult result;
    result.status = validateChunk(chunk, config);
    if (result.status != ExtractStatus::Ok) {
        return result;
    }
    std::uint64_t hash = 0;
    std::size_t position = 0;
    for (std::size_t i = 0; i < FEATURE_COUNT; ++i) {
        const auto end = subchunkBoundary(chunk.length, i + 1);
        auto maximum = FingerprintValue {0};
        for ( ; position < end; ++position) {
            hash = gearRoll(hash, chunk.data[position]);
            maximum = std::max(maximum, hash);
        }
        result.features[i] = maximum;
    }
    result.superFeatures = hashSuperFeatures(groupFinesseFeatures(result.features));
    return result;
}
