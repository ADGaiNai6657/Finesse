#pragma once

#include "Common/Types.h"

auto approvedTransformParameters() -> TransformParameters;
auto validateTransformParameters(const TransformParameters &parameters) -> bool;
auto transformFingerprint(std::uint64_t fingerprint,
                          std::uint32_t multiplier,
                          std::uint32_t addend) -> std::uint32_t;
auto groupNTransformFeatures(const FeatureValues &features) -> FeatureGroups;
auto extractNTransform(ChunkView chunk, const HashConfig &config,
                       const TransformParameters &parameters) -> SignatureResult;
