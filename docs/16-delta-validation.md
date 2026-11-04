# D-22～D-24 Delta 验收与实跑记录

本文区分自动检查、单次真实数据质量运行及独立 CSV/源摘要审计。协议见 [15](15-xdelta-pipeline-proposal.md)，阅读入口见 [13](13-code-review-guide.md)。单次构造不作为稳定系统吞吐或论文结论复现。

## 验证方法

- Debug 使用 FINESSE_SANITIZERS=ON，ASan/UBSan 同时覆盖 C++ 与静态 xdelta C 核心。
- Release 使用相同功能与冻结策略、优化构建，运行全部六个 CTest 目标。
- DeltaStoreTests 直接验证真正 VCDIFF roundtrip、编辑/插删/空/二进制/不同长度、损坏/错误基块、无收益 Raw、释放原文后恢复、Raw-only 索引、非法引用、重复不重编码。
- DeltaExperimentTests 运行真实 CLI，覆盖严格配置、相对路径、raw/manifest 重放、单次不运行 SF、拒绝覆盖、失败无正常汇总与空分母。
- 四组当前 FastCDC 输入分别构造两个编码池；释放原文池后恢复所有出现。每组每算法一次，结果写独立 delta-release-v1 目录。
- verify_delta_results.py 独立检查源文件 SHA-256、manifest 范围摘要与恢复结果摘要、SHA-1 元信息、引用图、全部计数与加权比例。它不在进程退出后重新加载 payload；实际 payload 恢复发生在 C++ 进程内。

## 运行结果

2026-11-04 已完成：Debug ASan/UBSan **6/6**，Release **6/6**，23 个上游文件 Git blob/长度/SHA-256 全部一致；四组 C++ 实跑及独立核验全部通过。sanitizer 日志未报告错误。每组目录的 validation.txt 为执行后补记，附对应 CTest 原始日志；自动 CLI 本身不生成该补记。

所有四组本次运行的构建 sourceDigest 相同：

```text
139e379110f0efe0e0a0b96d90b31483be958a741b918840b6ab7fc9ffbc7e81
```

该次构建保存的 source-files.csv 是确切构建输入，包含执行前本页状态；本页随后补入实测记录及文档排版修订，不改写历史 source-files 快照。后续重建时 sourceDigest 变化属正常，复核本次结果应使用各自保存的快照。

| 组 | 算法 | 唯一原文字节 | 最终 payload 字节 | Raw 块 | Delta 块 | DCR | payload 节省 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | Finesse-Gear | 179,742,046 | 179,742,046 | 17,483 | 0 | 1.000000 | 0.0000% |
| 1 | NTransform-Gear | 179,742,046 | 179,734,843 | 17,482 | 1 | 1.000040 | 0.0040% |
| 2 | Finesse-Gear | 428,027,351 | 311,066,472 | 36,553 | 12,424 | 1.376000 | 27.3256% |
| 2 | NTransform-Gear | 428,027,351 | 258,893,254 | 29,923 | 19,054 | 1.653297 | 39.5148% |
| 3 | Finesse-Gear | 473,162,576 | 367,021,258 | 31,797 | 8,597 | 1.289197 | 22.4323% |
| 3 | NTransform-Gear | 473,162,576 | 306,479,680 | 26,381 | 14,013 | 1.543863 | 35.2274% |
| 4（synthetic） | Finesse-Gear | 41,692,808 | 38,559,669 | 3,484 | 140 | 1.081254 | 7.5148% |
| 4（synthetic） | NTransform-Gear | 41,692,808 | 36,722,694 | 3,327 | 297 | 1.135342 | 11.9208% |

DCR 与节省均以**已经精确去重后的唯一原文字节**为分母基础，最终 payload 含未匹配/短块/回退 Raw；不含索引、元数据或内存分配开销。组1为压缩包，Finesse 无候选且全部 Raw，NTransform 只有1个接受的 Delta。所有四组本次匹配尝试恰好均有正收益，实际无 Raw 回退；这不证明回退路径不会出现，该路径由真实 codec 的不相关候选单元测试覆盖。

| 组 | 每算法恢复出现数 | 每算法恢复字节 | 结果目录 |
| --- | --- | --- | --- |
| 1 | 17,486 | 179,761,540 | [DataSet_1](../results/fastcdc-dataset-1-delta-release-v1/run.txt) |
| 2 | 75,132 | 636,563,018 | [DataSet_2](../results/fastcdc-dataset-2-delta-release-v1/run.txt) |
| 3 | 60,453 | 650,055,680 | [DataSet_3](../results/fastcdc-dataset-3-delta-release-v1/run.txt) |
| 4 | 24,931 | 268,435,456 | [DataSet_4](../results/fastcdc-dataset-4-delta-release-v1/run.txt) |

每算法总计恢复 **178,002 次出现、1,734,815,694 字节**；两算法合计356,004次。恢复在构造原文池销毁后进行，所有长度和 SHA-256 通过，构建时每次编码另做立即解码/全部字节比较。两算法的 Raw/Delta 表示、基块索引相互独立。

本次命令：

```sh
cmake --build build/debug -j 4
ctest --test-dir build/debug --output-on-failure
cmake --build build/release -j 4
ctest --test-dir build/release --output-on-failure
build/release/Finesse --delta-config configs/fastcdc-dataset-1-delta.conf
build/release/Finesse --delta-config configs/fastcdc-dataset-2-delta.conf
build/release/Finesse --delta-config configs/fastcdc-dataset-3-delta.conf
build/release/Finesse --delta-config configs/fastcdc-dataset-4-delta.conf
python3 scripts/verify_delta_results.py results/fastcdc-dataset-1-delta-release-v1 results/fastcdc-dataset-2-delta-release-v1 results/fastcdc-dataset-3-delta-release-v1 results/fastcdc-dataset-4-delta-release-v1
```

现有结果目录拒绝覆盖。重跑请复制配置并为 outputDirectory/runId 使用新值。结果 CSV 是元信息/恢复摘要证据，进程退出后实际 payload 已释放，不支持从 CSV 跨进程重载。

## 未实现与结论限制

磁盘容器、跨进程 Raw/Delta 重载、论文400MB LRU、并行流水线、多层链和原论文六数据集仍未实现。单次构造、编码及恢复的纳秒观察保存在 CSV，未据此报告稳定系统吞吐倍数。原 SF 五轮计时保持独立；本次结果只能说明当前 FastCDC/Gear 变体、Raw-only/深度1规则及固定 codec 下的 payload 质量和进程内恢复正确性，不是原论文系统或 Rabin 的逐位复现。
