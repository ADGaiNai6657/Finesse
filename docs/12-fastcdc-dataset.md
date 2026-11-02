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
- 结果为 `results/fastcdc-dataset-N-release-v1/`，已有目录拒绝覆盖。
- 每组 `iterations=1`，每算法预热一次、正式五次，单线程轮换，`diagnostics=true`。继续使用 D-16 CDC 参数及冻结 Gear 语义，不启用去重与 Delta 压缩。

```sh
python3 scripts/prepare_fastcdc_inputs.py
cmake --build build/release -j 4
build/release/Finesse --config configs/fastcdc-dataset-1.conf
build/release/Finesse --config configs/fastcdc-dataset-2.conf
build/release/Finesse --config configs/fastcdc-dataset-3.conf
build/release/Finesse --config configs/fastcdc-dataset-4.conf
```

现有清单无需重复生成，数据变更应指定新清单目录并更新配置。从 `build/release` 启动则使用 `./Finesse --config ../../configs/fastcdc-dataset-N.conf`，读取相同输入。
