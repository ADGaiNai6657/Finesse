#pragma once

#include "BreakingApart.h"

//检查 CDC 配置是否等于已批准的 D-16 参数
//输入：config：待检查的块长、NC 等级和三种掩码。
//输出：返回是否全部一致，不修正或截断调用者配置。
auto validateCDCConfig(const BreakingApartConfig &config) -> bool;
//校验配置后分块，并检查切点递增和完整覆盖
//输入：data：只读文件字节；config：必须符合 D-16 的 CDC 配置。
//输出：返回切点数组；空文件返回空数组，配置、溢出或切点异常时抛异常。
auto chunkBoundaries(std::string_view data, const BreakingApartConfig &config)
    -> std::vector<std::size_t>;
