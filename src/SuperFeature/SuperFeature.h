#pragma once

#include "Common/Types.h"

using EncodedFeatureGroup = std::array<std::uint8_t, FEATURES_PER_SUPER_FEATURE * FEATURE_BYTES>;

auto serializeFeatures(const FeatureGroup &features) -> EncodedFeatureGroup;
auto hashSuperFeatures(const FeatureGroups &groups) -> SuperFeatures;
