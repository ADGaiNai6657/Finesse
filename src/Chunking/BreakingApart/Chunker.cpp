#include "Chunker.h"

#include <limits>
#include <stdexcept>

//检查 CDC 配置是否等于已批准的 D-16 参数
//输入：config：待检查的块长、NC 等级和三种掩码。
//输出：返回是否全部一致，不修正或截断调用者配置。
auto validateCDCConfig(const BreakingApartConfig &config) -> bool {
    // 首期仅支持 D-16；拒绝零最小值，避免上游无进展路径，不能 clamp 后继续。
    const auto approved = makeFastCDCConfig(8192, 2048, 0);
    return config.MinSize == approved.MinSize && config.NormalSize == approved.NormalSize
        && config.MaxSize == approved.MaxSize && config.NcLevel == approved.NcLevel
        && config.MaskA == approved.MaskA && config.MaskS == approved.MaskS && config.MaskL == approved.MaskL;
}

//校验配置后分块，并检查切点递增和完整覆盖
//输入：data：只读文件字节；config：必须符合 D-16 的 CDC 配置。
//输出：返回切点数组；空文件返回空数组，配置、溢出或切点异常时抛异常。
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
