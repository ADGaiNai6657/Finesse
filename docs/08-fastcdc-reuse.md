# FastCDC 摘取来源与适配清单

## 已核实版本

来源仓库：[ADGaiNai6657/FastCDC](https://github.com/ADGaiNai6657/FastCDC)。用户指出完整代码位于非 main 分支，已通过 GitHub 分支列表及源码读取核实：

- 分支：`feature/chunk-store-dedup`。
- 固定提交：[`be5816e8e2e8548738db3e7dd54aa323056df402`](https://github.com/ADGaiNai6657/FastCDC/tree/be5816e8e2e8548738db3e7dd54aa323056df402)。
- 本机 FastCDC HEAD 与该远端提交相同，工作区检查未显示修改。
- `main` 的 `de52ffdf...` 仅为初版，不再作为摘取基准。

本文件记录代码摘取设计。首期现已原样摘取 GearHash 与 BreakingApart；实际文件 SHA-256 和新增适配见 10，不把本文件历史核实记录当作运行结果。

## 模块和复用边界

| 来源 | 已核实内容 | 摘取用途 |
| --- | --- | --- |
| `src/GearHash/GearHash.h/.cpp` | 256 项 64 位表，`gearRoll`，`updateGearHash` | CDC 与两个 SF 算法共用相同更新原语，各自持有独立状态 |
| `src/BreakingApart/BreakingApart.h/.cpp` | `BreakingApartConfig`、`makeFastCDCConfig`、`BoundariesFinder` | 首期 CDC 输入适配，保留切点语义 |
| `src/Hash/Hash.h/.cpp` | OpenSSL EVP SHA-1 封装 | 启用去重输入模式或后续系统时复用，不用 Gear 代替精确内容摘要 |
| `src/ChunkStore/ChunkStore.h/.cpp` | 唯一块池、SHA-1 索引、出现记录、`resetStore` | 去重输入模式的来源；避免算法提取依赖其可变全局状态 |
| `src/main.cpp` | 文件读取与排序、CDC 调用、统计与 CSV | 摘取输入驱动思路及必要函数，适配到已批准 Experiment 协议 |
| `CMakeLists.txt` | C++20、OpenSSL 查找和链接 | 按所启用模块选择依赖，保留本工程 CMake 4.2 |

来源没有 Finesse 排名分组、N-transform 的 12 对参数、SF 序列化协议或跨槽相似索引；这些仍由本文档定义，不将新代码标为上游已有。

## 已观察到的运行语义

Gear 更新为 `(hash << 1) + GEAR_TABLE[byte]`，使用 `std::uint64_t` 自然模 2^64 运算。`updateGearHash` 不自行清零，调用方决定起始状态。

CDC 每个 chunk 开始 hash=0，从 `last+MinSize` 才开始滚动；命中时切点为 i，当前 chunk 对应 `[last,i)`，而用于判定的 `data[i]` 不属于该 chunk。摘取时应保留并测试这一语义，不擅自改成 i+1。这也说明 CDC 的中间 hash 不能直接供 Finesse 特征提取复用。

`makeFastCDCConfig` 将 NC 等级限制在 0～3，最大长度为 expected×8。来源 CLI 默认 expected=8192、min=expected/4、NC=0；NC 开启时 NormalSize=max(expected,min+expected/2)。这些是源码行为，不代表 Finesse 原论文设置。已批准 D-16 沿用默认数值，并在配置中显式保存实际掩码。

源驱动将路径排序后逐文件分块；内容索引可跨文件保留。是否启用其去重模式必须与“所有 CDC chunk”微基准区分。`ChunkStore` 暴露全局状态，实验运行边界必须 reset，不能让预热或上一算法污染下一次测量。

## 适配时必须处理的差异

- 来源 `dedupRatio` 是 `1-uniqueBytes/totalBytes`，本项目的 `dedupRatio` 是 `totalBytes/uniqueBytes`。迁移时按公式映射字段，前者另命名为 `dedupSavingRatio`，不能同名直接复制。
- 来源输出中 MBps 实际以 1024² 除数计算；本项目 MB/s 按 10⁶。保留原始字节和秒，重新按已批准单位计算。
- 来源驱动按一轮全流程统计，不能直接充当本项目的完整 SF 微基准计时区间。
- 参数校验应在适配层执行：正期望长度、合法 NC、最小/最大顺序、长度和乘法不溢出。来源的 clamp 行为不能掩盖输入配置错误。
- 源码静态检查发现 `MinSize=0` 时可能出现 `cut==last` 的无进展路径；适配阶段必须覆盖此测试。默认 D-16 的 min=2048 不触发该特定入口。若要支持零最小长度，需要单独确认修复语义。
- 读取或遍历失败应使实验失败，不能像来源驱动部分路径那样继续处理后仅汇总成功文件。此适配遵从已批准的实验错误契约。

摘取时保留来源提交、原路径、文件摘要和适配差异清单。新模块按本项目风格，直接摘取接口保留原名称以利追踪。实际摘取记录见 10；没有修改上游仓库。
