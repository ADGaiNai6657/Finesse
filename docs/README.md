# Finesse 实验框架设计文档

版本：0.3，2026-11-01。首期设计已批准并已据此建立实现；新增 D-17/D-18 审批记录见 09。后续完整系统 D-13 延后。构建与使用说明见 [根目录 README](../README.md)，测试与运行状态以实际验证记录为准。

## 已确认的范围

- 首期实现 Finesse 与 N-transform SF 的算法对比，预留完整去重后 Delta 压缩系统接口。用户已确认，见 D-01。
- 代码风格遵从用户指定的 [FastCDC 仓库](https://github.com/ADGaiNai6657/FastCDC)，见 D-02。
- 对论文 DCE 的矛盾，同时保留剩余比例与节省比例，不直接把其中一项认定为论文 DCE。用户已确认，见 D-03。

首期交付目标是能够对相同 chunk 输入计算两种算法的 features、super-features，验证计算结果，并公平比较 SF 计算速度。相似块匹配可以作为独立的诊断实验；没有编码器和完整输入管线时，不输出 DCR、DCE 或系统吞吐量结论。

已批准改用 GearHash，并从 FastCDC 的 `feature/chunk-store-dedup` 分支摘取基础设施及 CDC。首期对比名称为 **Finesse-Gear / NTransform-Gear**，两者使用同一 FastCDC 输入序列；它们是论文算法的工程变体。首期 D-01～D-12、D-14～D-16 均已批准；具体参数见审批表。首期对全部 CDC 块做微基准，长度小于 12 的块按 D-04 排除，不启用去重输入模式。

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

## 如何使用

文中 `[论文]` 表示论文直接明确的内容，`[已批准 D-xx]` 表示用户已作出的选择，`[提案 D-xx]` 表示尚未批准的工程设计。为便于阅读，一节开头的状态标记适用于整节，直至出现新的状态标记。

尚未批准的提案即使使用“应当”“必须”描述契约，也不自动成为实施授权。实现者应先检查决策表；不得通过默认参数、硬编码、替换算法或忽略尾部数据绕过待审批项。新发现的歧义应补充到决策表并向用户询问。

首期已建立 `src/`、`tests/`、`configs/`、`scripts/`，原始 Hello World 入口已替换；实际模块与摘取摘要见根目录 README 和 10。

真实数据路径尚未指定；D-17 已批准 synthetic 验收样例的规模、生成器和种子，每次运行记录实际配置。真实数据集实验仍需指定数据路径，不将验收样例当作真实数据集结论。
