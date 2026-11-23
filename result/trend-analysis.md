# Finesse 实验复现了哪些论文趋势？

结论是部分复现。Finesse-Gear 在四组负载上计算超级特征（SF）均快约 3 倍，内存管线吞吐也全部提高。最大的偏差出现在整体压缩比：在 Emacs 展开源码和未压缩 tar 上，Finesse-Gear 的 DCR 比 NTransform-Gear 低约六分之一。

本分析复用 2026-11-23 的 Apple M5 单线程 Release 实验，代码版本为 6e4abef。两种算法共享每组去重后的输入。以下倍数和百分比均以同一组的 NTransform-Gear 为基线。

## 速度：四组均复现约三倍的 SF 加速

| 负载 | SF 速度倍数 | 内存管线吞吐增幅 | DCR 相对变化 |
| --- | --- | --- | --- |
| Emacs 压缩包 | 3.07× | +43.3% | -0.004% |
| Emacs 展开源码 | 3.08× | +25.7% | -16.77% |
| Emacs tar | 3.18× | +55.0% | -16.50% |
| 合成备份 | 2.94× | +13.6% | -4.76% |

论文图 4 报告 i7 和 E5 上的平均 SF 加速分别为 3.5× 和 3.2×。本机各算法各测五轮，先求逐轮速度的算术平均，再取两算法之比；四组倍数等权平均为 3.07×。每一组中，Finesse 最慢一轮的 SF 速度仍高于基线最快一轮。

论文图 5 报告系统吞吐提高 41%–85%。本机第 1、3 组落在这个区间，第 2、4 组低于其下限。SF 只计有效唯一块的签名提取；内存管线吞吐按全部输入字节计算，从读取输入列表到完成 Delta 编码池构建，包含分块、去重和即时解码核验。

[80 条正式测速记录](clean/formal-rounds.csv) · [本机测速范围](clean/figures4-5-rates.csv) · [论文图 4](paper/figure4.png) · [论文图 5](paper/figure5.png)

## 压缩：差距集中在源码和未压缩 tar 的匹配覆盖量

DCR 等于去重后唯一块的原始字节数除以最终 Raw 与 Delta 的总字节数，越高表示占用越小。论文表 3 中 Finesse 的 DCR 相对变化为 −3.21% 至 +7.36%。本机第 1 组两算法几乎持平，第 4 组低 4.76%，第 2、3 组的降幅均超过 16%。

下表每格按 Finesse-Gear / NTransform-Gear 排列。匹配覆盖量是进入 Delta 尝试的目标原文字节总和。匹配节省率为 1 减去全部尝试的 Delta 字节总和与目标原文字节总和之比。MB 取 1,000,000 字节。

| 负载 | DCR | 匹配块数 | 匹配覆盖量（MB） | 匹配节省率 |
| --- | --- | --- | --- | --- |
| Emacs 压缩包 | 1.00000 / 1.00004 | 0 / 1 | 0.00 / 0.01 | 无匹配 / 66.25% |
| Emacs 展开源码 | 1.37600 / 1.65330 | 12,424 / 19,054 | 118.90 / 175.55 | 98.37% / 96.35% |
| Emacs tar | 1.28920 / 1.54386 | 8,597 / 14,013 | 107.74 / 173.53 | 98.52% / 96.06% |
| 合成备份 | 1.08125 / 1.13534 | 140 / 297 | 3.63 / 5.85 | 86.30% / 84.99% |

第 2、3 组的匹配尝试全部产生正收益。Finesse 命中后的节省率更高，但分别少覆盖 56.6 MB 和 65.8 MB 的目标原文，更多唯一块以 Raw 保存。两组合计最终多占 112.7 MB，占四组最终字节差额的 98.4%。这解释了为何局部匹配质量提高，整体压缩比仍会下降。

在有样本可比的第 2–4 组，Finesse 的匹配节省率均更高，与论文表 3 对匹配质量的解释方向一致。论文 §5.1 将 DCE 写为压缩后大小除以压缩前大小，同时用较高值解释较好的匹配质量，因此这里只比较明确定义的字节节省率。

[全部压缩字节与比例](clean/table3-compression.csv) · [论文表 3](paper/table3.png)

## 局部性：特征比内容更容易保留，强度低于论文

| 负载 | 候选块对数 | 平均内容相同子块数 / 12 | 平均特征相同子块数 / 12 |
| --- | --- | --- | --- |
| Emacs 压缩包 | 1 | 0.00 | 0.00 |
| Emacs 展开源码 | 19,054 | 1.54 | 8.25 |
| Emacs tar | 14,013 | 1.96 | 8.07 |
| 合成备份 | 297 | 3.37 | 5.86 |

样本取 NTransform-Gear 实际命中的目标块与 Raw 基块对，比较 12 个对应位置。论文六组平均有 5.78–9.19 个子块内容相同、10.04–10.97 个子块特征相同。本机第 2–4 组也能观察到内容变化后仍保留特征，但特征相同的均值为 5.86–8.25 个。第 1 组仅有一个块对，两个零值只描述该样本。

[本机表 1](clean/table1-locality.csv) · [论文表 1](paper/table1.png)

## 如何理解与论文的差异

本机输入包括三组 Emacs 数据和一组合成备份。论文使用六组不同的备份负载。本机使用 FastCDC 与 Gear，基块限定为先前的 Raw，Delta 深度上限为 1；论文使用 Rabin、Destor 流水线和 400 MB LRU 基块缓存，测速平台为 Intel i7/E5。

这组数据可以定位整体压缩差额来自哪些负载、哪些字节，也显示这些负载的对应子块局部性较弱。工作负载、哈希和分块策略同时改变，实验无法单独确定其中哪一项导致候选变少。

[逐组趋势 CSV](clean/trend-comparison.csv) · [完整实验报告](report.html) · [原始输入归档](raw/source-inputs.tar) · [原始系统计时](raw/supplement/throughput.csv) · [论文全文](paper/FAST19-Finesse.pdf)

[第 1 组原始 SF 计时](raw/ds1-sf/measurements.csv) · [逐对局部性](raw/supplement/ds1-locality.csv) · [Delta 汇总](raw/ds1-delta/compression-summary.csv)

[第 2 组原始 SF 计时](raw/ds2-sf/measurements.csv) · [逐对局部性](raw/supplement/ds2-locality.csv) · [Delta 汇总](raw/ds2-delta/compression-summary.csv)

[第 3 组原始 SF 计时](raw/ds3-sf/measurements.csv) · [逐对局部性](raw/supplement/ds3-locality.csv) · [Delta 汇总](raw/ds3-delta/compression-summary.csv)

[第 4 组原始 SF 计时](raw/ds4-sf/measurements.csv) · [逐对局部性](raw/supplement/ds4-locality.csv) · [Delta 汇总](raw/ds4-delta/compression-summary.csv)
