#include "SuperFeature.h"

#include "GearHash/GearHash.h"

//把features处理成byte串
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
auto hashSuperFeatures(const FeatureGroups &groups) -> SuperFeatures {
    SuperFeatures result {};
    for (std::size_t s = 0; s < groups.size(); ++s) {
        const auto bytes = serializeFeatures(groups[s]);
        std::uint64_t hash = 0;
        result[s] = updateGearHash(bytes.data(), bytes.size(), hash);
    }
    return result;
}
