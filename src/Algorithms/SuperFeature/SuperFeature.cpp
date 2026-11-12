#include "SuperFeature.h"

#include "GearHash/GearHash.h"

//把features处理成byte串
//把一个 SF 的四个 features 处理成固定字节串
//输入：features：按组合顺序排列的四个 uint64 值。
//输出：返回 32 字节数组，每个值按 8 字节小端编码，不依赖主机端序。
auto serializeFeatures(const FeatureGroup &features) -> EncodedFeatureGroup {
    EncodedFeatureGroup bytes {};
    for (std::size_t i = 0; i < features.size(); ++i) {
        for (std::size_t b = 0; b < FEATURE_BYTES; ++b) {
            bytes[i * FEATURE_BYTES + b] = static_cast<std::uint8_t>(features[i] >> (b * 8));   //将feature按字节（8 bit）拆分出来给byte，byte用于获取SF
        }
    }
    return bytes;
}

//计算SF；将groups按照下标处理成byte后压入hash，得到SF[s]
//将各组 features 编码并计算对应的 SF
//输入：groups：三个有序分组，每组四个 feature。
//输出：返回 3 个 SF；每组先编码成字节，再从零 Gear 状态滚入取最终值。
auto hashSuperFeatures(const FeatureGroups &groups) -> SuperFeatures {
    SuperFeatures result {};
    for (std::size_t s = 0; s < groups.size(); ++s) {
        const auto bytes = serializeFeatures(groups[s]);
        std::uint64_t hash = 0;
        result[s] = updateGearHash(bytes.data(), bytes.size(), hash);
    }
    return result;
}
