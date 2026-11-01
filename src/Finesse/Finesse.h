#pragma once

#include "Common/Types.h"

auto groupFinesseFeatures(const FeatureValues &features) -> FeatureGroups;
auto extractFinesse(ChunkView chunk, const HashConfig &config) -> SignatureResult;
