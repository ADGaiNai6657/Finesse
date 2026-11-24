# Finesse 实验框架设计文档

版本：0.5，2026-11-04。当前 D-21 已批准接入 SHA-1 精确去重、内存唯一块池，默认唯一块 SF，保留 all 对照入口。D-22～D-24 已批准并接入真实 Xdelta 与可恢复内存 Raw/Delta 管线；磁盘容器、400MB LRU、并行及跨进程重载仍待 D-13。构建与使用见 [根目录 README](../README.md)，状态以实际验证记录为准。

2026-11-12 已按职责整理 src 目录，算法、哈希、分块、压缩、存储、运行各自归组，源码内容保持原样；目录和构建引用见 [03](03-architecture.md)，阅读链接见 [13](13-code-review-guide.md)。

准备做代码 Review 时，建议先读 [代码 Review 导读](13-code-review-guide.md)：包含阅读顺序、模块职责、关键调用路径和逐项核对清单。

代理工作约定见 [AGENT.md](../AGENT.md)（[AGENTS.md](../AGENTS.md) 自动入口）。与论文/文档冲突或未批准的取舍必须报告/询问人类，修改实现须同步对应文档。

## 已确认的范围

- 首期实现 Finesse 与 N-transform SF 的算法对比，预留完整去重后 Delta 压缩系统接口。用户已确认，见 D-01。
- 代码风格遵从用户指定的 [FastCDC 仓库](https://github.com/ADGaiNai6657/FastCDC)，见 D-02。
- 对论文 DCE 的矛盾，同时保留剩余比例与节省比例，不直接把其中一项认定为论文 DCE。用户已确认，见 D-03。

首期交付目标是能够对相同 chunk 输入计算两种算法的 features、super-features，验证计算结果，并公平比较 SF 计算速度。相似块匹配可以作为独立诊断；新增 Delta 入口真实编码、恢复并报告 payload DCR，和 SF 微基准分开。DCE 歧义继续按 D-03 保留，单次质量验证不报告稳定系统吞吐。

已批准以 Gear 替换 Rabin，并从 FastCDC 功能分支摘取/适配基础设施，名称仍为 Finesse-Gear / NTransform-Gear。D-21 更新原先 D-16 的未去重口径：所有块先经 SHA-1＋长度/字节确认进入唯一池，默认显式 sfInput=unique，all 保留每次出现的对照遍历。两算法共享同一所选集合，短块统一排除；精确存储本身保留唯一短块。

## 阅读顺序

| 文档 | 用途 |
| --- | --- |
| [01-paper-evidence.md](01-paper-evidence.md) | 论文事实、页码、公式、实验结果与疑点 |
| [02-algorithm-spec.md](02-algorithm-spec.md) | 两种算法的技术规格、输入输出和边界条件 |
| [03-architecture.md](03-architecture.md) | 模块、目录、数据流、实施阶段 |
| [04-interfaces.md](04-interfaces.md) | C++ 接口契约、所有权、状态与错误契约 |
| [05-coding-style.md](05-coding-style.md) | 从 FastCDC 提取的代码写法与补充规范 |
| [06-experiments.md](06-experiments.md) | 实验配置、指标、测试与验收要求 |
| [08-fastcdc-reuse.md](08-fastcdc-reuse.md) | 已核实的分支、模块与适配要求 |
| [07-decisions.md](07-decisions.md) | 审批记录、待决策方案及其影响 |
| [09-configuration-proposal.md](09-configuration-proposal.md) | 已批准 D-17 配置、运行样例和 D-18 空文件摘要补充 |
| [10-source-provenance.md](10-source-provenance.md) | 实际摘取文件摘要与适配边界 |
| [11-tttd-dataset.md](11-tttd-dataset.md) | D-19 历史方案：TTTD-S 两组数据 |
| [12-fastcdc-dataset.md](12-fastcdc-dataset.md) | D-20 当前方案：FastCDC 四组数据、分类与相对路径 |
| [13-code-review-guide.md](13-code-review-guide.md) | 面向代码 Review 的阅读路线、实现细节与核对清单 |
| [14-sha1-chunk-store.md](14-sha1-chunk-store.md) | D-21：精确查重、内存唯一池、引用与统计契约 |
| [15-xdelta-pipeline-proposal.md](15-xdelta-pipeline-proposal.md) | 已批准 D-22～D-24：真实 xdelta、Raw/Delta 存储、配置、恢复与指标 |
| [16-delta-validation.md](16-delta-validation.md) | Debug/Release 验收和四组 Delta 实跑证据 |
| [17-timer-asking.md](17-timer-asking.md) | 通用实验统计、计时与导出需求 |
| [18-experiment-instrumentation.md](18-experiment-instrumentation.md) | 实现、字段、运行模式与基准证据 |

## 如何使用

文中 `[论文]` 表示论文直接明确的内容，`[已批准 D-xx]` 表示用户已作出的选择，`[提案 D-xx]` 表示尚未批准的工程设计。为便于阅读，一节开头的状态标记适用于整节，直至出现新的状态标记。

尚未批准的提案即使使用“应当”“必须”描述契约，也不自动成为实施授权。实现者应先检查决策表；不得通过默认参数、硬编码、替换算法或忽略尾部数据绕过待审批项。新发现的歧义应补充到决策表并向用户询问。

首期已建立 `src/`、`tests/`、`configs/`、`scripts/`，原始 Hello World 入口已替换；实际模块与摘取摘要见根目录 README 和 10。

当前按 D-20 使用同级 FastCDC 的四组 Dataset，以相对路径读取并排除 macOS 元数据，见 12；第四组明确为 synthetic，D-17 小型 synthetic 样例继续用于算法验收。各组记录实际来源和配置，不将它们当作 Finesse 原始六数据集。
