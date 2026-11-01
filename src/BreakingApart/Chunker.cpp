#include "Chunker.h"

#include <limits>
#include <stdexcept>

auto validateCDCConfig(const BreakingApartConfig &config) -> bool {
    // 首期仅支持 D-16；拒绝零最小值，避免上游无进展路径，不能 clamp 后继续。
    const auto approved = makeFastCDCConfig(8192, 2048, 0);
    return config.MinSize == approved.MinSize && config.NormalSize == approved.NormalSize
        && config.MaxSize == approved.MaxSize && config.NcLevel == approved.NcLevel
        && config.MaskA == approved.MaskA && config.MaskS == approved.MaskS && config.MaskL == approved.MaskL;
}

auto chunkBoundaries(const std::string_view data, const BreakingApartConfig &config)
    -> std::vector<std::size_t> {
    if (!validateCDCConfig(config)) {
        throw std::invalid_argument("CDC configuration differs from approved D-16");
    }
    if (data.size() > std::numeric_limits<std::size_t>::max() - config.MaxSize) {
        throw std::length_error("CDC input too large for upstream offset arithmetic");
    }
    std::vector<std::vector<std::size_t>> groups;
    BoundariesFinder(data, groups, config);
    auto cuts = std::move(groups.back());
    std::size_t previous = 0;
    for (const auto cut : cuts) {
        if (cut <= previous || cut > data.size()) {
            throw std::runtime_error("CDC produced an invalid or non-progressing cut");
        }
        previous = cut;
    }
    if (previous != data.size()) {
        throw std::runtime_error("CDC did not cover complete input");
    }
    return cuts;
}
