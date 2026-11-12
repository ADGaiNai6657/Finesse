//
// BreakingApart.h
// FastCDC 边界查找（论文 Algorithm 1）的对外接口与参数。
//

#ifndef FASTCDC_BREAKINGAPART_H
#define FASTCDC_BREAKINGAPART_H

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

// FastCDC 分块参数。NC 的归一化等级由 NcLevel 表示：
//   0 = 关闭 NC（统一用 MaskA）；1/2/3 = 归一化等级 1/2/3。
// 掩码位数：base = log2(ExpectedSize)；NC level L 前段用 base+L 位、后段用 base-L 位。
struct BreakingApartConfig {
    std::size_t MinSize;      // 最小块长（切点跳过区间）。
    std::size_t NormalSize;   // 归一化切换点（NC 时 = max(ExpectedSize, MinSize + ExpectedSize/2)）。
    std::size_t MaxSize;      // 最大块长（强制切分）。
    std::uint64_t MaskA;      // 非 NC 掩码（13 位，对应 Algorithm 1 的 MaskA）。
    std::uint64_t MaskS;      // NC 在 NormalSize 之前使用的较难命中掩码。
    std::uint64_t MaskL;      // NC 在 NormalSize 之后使用的较易命中掩码。
    int NcLevel;              // 0=off, 1/2/3。
};

// 依据期望块长 / 最小块长 / NC 等级构造配置。
// 8KB 的 11/13/15 位掩码使用论文公开常量；其余位宽按同样的零填充分布生成。
//根据块长和 NC 等级构造 FastCDC 参数
//输入：expectedSize：期望块长；minSize：最小块长；ncLevel：NC 等级，按现有规则限制到 0～3。
//输出：返回含块长、掩码和 NC 等级的配置；批准参数的严格检查由外层完成。
auto makeFastCDCConfig(std::size_t expectedSize,
                       std::size_t minSize,
                       int ncLevel) -> BreakingApartConfig;

// 对一段缓冲查找切点，写入 vPosition 的末尾一组（绝对字节偏移，末项 == data.size()）。
//扫描一段缓冲并追加一组 FastCDC 切点
//输入：data：只读文件字节；vPosition：待追加的切点分组；config：分块参数。
//输出：无返回值；在 vPosition 末尾追加一组切点，非空输入末切点为 data.size()，空输入追加空组。
auto BoundariesFinder(std::string_view data,
                      std::vector<std::vector<std::size_t>>& vPosition,
                      const BreakingApartConfig& config) -> void;

#endif //FASTCDC_BREAKINGAPART_H
