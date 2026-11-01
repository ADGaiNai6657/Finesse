#pragma once

#include "BreakingApart.h"

auto validateCDCConfig(const BreakingApartConfig &config) -> bool;
auto chunkBoundaries(std::string_view data, const BreakingApartConfig &config)
    -> std::vector<std::size_t>;
