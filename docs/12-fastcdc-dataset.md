# 当前数据：FastCDC 四组 Dataset（D-20）

用户明确指示“改用 FastCDC 的四组 Dataset”。当前输入来源据此由 D-19 切换到同级 `../FastCDC/Dataset`。沿用相对路径、各组独立测量、只读原数据和已批准的元数据筛选。TTTD-S 配置与结果保留为历史记录。

## 已核实输入

| 组 | 内容 | datasetKind | 文件数 | 字节数 |
| --- | --- | --- | --- | --- |
| DataSet_1 | Emacs `.tar.gz` 压缩包 | real | 5 | 179761540 |
| DataSet_2 | 5 个 Emacs 版本的展开源码（LNX 类） | real | 16982 | 636563018 |
| DataSet_3 | 未压缩 `.tar`（TAR 类） | real | 5 | 650055680 |
| DataSet_4 | 8 份 32 MiB 合成备份（SYN 类） | synthetic | 8 | 268435456 |

DataSet_2 递归枚举，另外三组枚举当前层普通文件。各组排序，拒绝符号链接，排除 `.DS_Store` 和 `._*`；当前仅 DataSet_2 排除一个 6148 字节的 `.DS_Store`。根目录自己的 `.DS_Store` 不在四组范围内。总输入 1734815694 字节，逐组运行，不同时驻留四组数据。

DataSet_1 的压缩输入单独报告；各组不混合，SYN 修改比例不作为相似性真值或压缩收益，也不将这些数据宣称为 Finesse 原始六数据集。

## 来源核验

DataSet_3 按 FastCDC 约定为 DataSet_1 gzip 流展开后的 tar。DataSet_4 来源脚本为 `../FastCDC/tools/make_synthetic_backups.py`，base=`DataSet_3/emacs-22.1.tar`，size=33554432、backups=8、rois=64、roi-size=8192、edit-inner=4096、seed=20260917。

核验只在内存计算预期内容，不调用会覆盖原数据的生成命令。本次运行的 tar 与备份来源一致性证据保存为结果目录的 provenance.txt；所有实际输入文件另记录 SHA-256。

## 入口与相对路径

- 准备脚本为 `scripts/prepare_fastcdc_inputs.py`，根目录参数相对 Finesse 项目解析，不依赖工作目录。
- 四份列表为 `configs/inputs-fastcdc/fastcdc-dataset-N.files.txt`，数据路径相对列表目录，例如 `../../../FastCDC/Dataset/DataSet_3/emacs-21.4a.tar`。
- 四份配置为 `configs/fastcdc-dataset-N.conf`，`inputPath=inputs-fastcdc/fastcdc-dataset-N.files.txt` 相对配置目录。
- 当前 D-21 结果为 `results/fastcdc-dataset-N-dedup-release-v1/`，已有目录拒绝覆盖；旧 `fastcdc-dataset-N-release-v1` 是启用存储前的历史结果。
- 每组 `iterations=1`，每算法预热一次、正式五次，单线程轮换，`diagnostics=true`、`sfInput=unique`。继续使用 D-16 CDC 参数与 Gear 语义，现已实际接入 SHA-1 精确去重和内存唯一池，此处为 SF 入口协议；真实 Delta 已由 D-22～D-24 单独接入。

去重范围为每组单次实验，跨该组所有文件；不跨四组混用池。重复出现保留原清单与 storedId 引用，默认仅首次出现的唯一块进入 SF。`sfInput=all` 可作原出现集合的对照，但存储仍只保留唯一 payload。两种模式的计时不得直接与旧数据量不同的结果相除。

```sh
python3 scripts/prepare_fastcdc_inputs.py
cmake --build build/release -j 4
build/release/Finesse --config configs/fastcdc-dataset-1.conf
build/release/Finesse --config configs/fastcdc-dataset-2.conf
build/release/Finesse --config configs/fastcdc-dataset-3.conf
build/release/Finesse --config configs/fastcdc-dataset-4.conf
```

现有清单无需重复生成，数据变更应指定新清单目录并更新配置。从 `build/release` 启动则使用 `./Finesse --config ../../configs/fastcdc-dataset-N.conf`，读取相同输入。

## D-22～D-24：相同四组的真实 Delta 质量验证

新增 configs/fastcdc-dataset-N-delta.conf（N=1..4），相对 inputConfig 引用本页原配置，数据列表/分类/排除规则/CDC/Gear 完全沿用；每组两算法独立构造一次并在原文池销毁后恢复全部出现。结果输出 results/fastcdc-dataset-N-delta-release-v1，旧 SF/dedup 目录不覆盖。第四组仍是 synthetic，不更名为论文原始 SYN。

原配置 iterations=1、预热/五轮和 diagnostics 只适用于 `--config`；Delta 入口的 pipelineRuns=1 是单次质量与恢复验证，忽略原微基准轮数及输出目录。配置、字节指标、恢复约束见 [15](15-xdelta-pipeline-proposal.md)，实跑与独立审计见 [16](16-delta-validation.md)。
