#include "NTransform.h"

#include "GearHash/GearHash.h"
#include "SuperFeature/SuperFeature.h"

#include <algorithm>

auto approvedTransformParameters() -> TransformParameters {
    // D-15；configs/parameters-v1.csv 保存相同参数及离线生成规则。
    return {{0x83dbcfb3U, 0xefe1f4edU, 0xb14d778dU, 0xb2b1a2e3U,
             0x57a2c881U, 0xa5b4b243U, 0x1ec06857U, 0x6b3c6633U,
             0x902c3e15U, 0xc784a19fU, 0x0f1935d7U, 0xdc5f7b3dU},
            {0x711864b8U, 0xa0b1eed0U, 0x1ef6a0f7U, 0x33bc7b0cU,
             0xc447a26fU, 0x8f3f43ecU, 0x734c14a1U, 0xd582363fU,
             0x2c003610U, 0xd956e23fU, 0xe9736070U, 0x49deff84U}};
}

auto validateTransformParameters(const TransformParameters &parameters) -> bool {
    const auto approved = approvedTransformParameters();
    return parameters.multipliers == approved.multipliers && parameters.addends == approved.addends;
}

//线性变换指纹（gearHash）
auto transformFingerprint(const std::uint64_t fingerprint,
                          const std::uint32_t multiplier,
                          const std::uint32_t addend) -> std::uint32_t {
    // 最大乘加结果仍在 uint64_t 范围内，最后明确截取低 32 位。
    const auto low = static_cast<std::uint32_t>(fingerprint);
    return static_cast<std::uint32_t>(static_cast<std::uint64_t>(multiplier) * low + addend);
}

//将一维的features处理成二维的groups，不涉及线性变换
auto groupNTransformFeatures(const FeatureValues &features) -> FeatureGroups {
    FeatureGroups groups {};
    for (std::size_t s = 0; s < SUPER_FEATURE_COUNT; ++s) {
        std::copy_n(features.begin() + s * FEATURES_PER_SUPER_FEATURE,
                    FEATURES_PER_SUPER_FEATURE, groups[s].begin());
    }
    return groups;
}

auto extractNTransform(const ChunkView chunk,
                       const HashConfig &config,
                       const TransformParameters &parameters) -> SignatureResult {
    SignatureResult result;
    result.status = validateChunk(chunk, config);
    if (result.status != ExtractStatus::Ok) {
        return result;
    }
    if (!validateTransformParameters(parameters)) {
        result.status = ExtractStatus::InvalidConfig;
        return result;
    }
    std::uint64_t hash = 0;
    for (std::size_t j = 0; j < chunk.length; ++j) {
        hash = gearRoll(hash, chunk.data[j]);
        for (std::size_t i = 0; i < FEATURE_COUNT; ++i) {
            const auto value = transformFingerprint(hash, parameters.multipliers[i], parameters.addends[i]);
            result.features[i] = std::max(result.features[i], static_cast<FingerprintValue>(value));
        }
    }
    result.superFeatures = hashSuperFeatures(groupNTransformFeatures(result.features));
    return result;
}
