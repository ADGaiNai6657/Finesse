#include "Finesse.h"

#include "GearHash/GearHash.h"
#include "SuperFeature/SuperFeature.h"

#include <algorithm>

//按组内排名将 12 个 features 组合成 3 个 SF 输入组
//输入：features：按子块顺序排列的 12 个值。
//输出：返回 3×4 的分组；先对四组各三个值稳定降序排序，再按相同排名跨组取值，原数组不变。
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

//提取 chunk 的 Finesse features 和 SF
//输入：chunk：只读字节，调用期间保持有效；config：已批准的 Gear 配置。
//输出：返回状态、12 个子块最大指纹和 3 个 SF；非法输入或短块通过状态表示。
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
