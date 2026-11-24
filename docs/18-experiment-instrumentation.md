# 实验统计框架

[需求](17-timer-asking.md) · [决策记录](07-decisions.md) · [旧版 Delta 字段](15-xdelta-pipeline-proposal.md)

## 调用路径

两条入口共用 `loadExperimentInput`。raw 模式逐文件读取、计算源 SHA-256、FastCDC 分块，再以 SHA-1 查找候选并逐字节确认重复。manifest 模式按原顺序加载已有范围。SF 入口在同一块集合上暖机一次并交替运行五轮，可另跑候选诊断。Delta 入口先完成精确去重，再分别构造两份 Raw/Delta 池。每个候选即时编码和逐字节校验，原文池释放后通过编码池恢复。

`Runtime/Instrumentation/Metrics` 管理线程局部采集、作用域标签和计时。`Runtime/Instrumentation/Run` 持有一次实验的配置、文件与块的元信息以及导出接口。采集器按算法、阶段计数，文件聚合额外带有 file_id。统计明细不引用 payload。当前两个生产入口串行运行，线程合并由单元测试验证。现有 FastCDC、Finesse、N-transform 和 Xdelta 内核沿用原执行顺序和参数。

恢复阶段由 `FileRecovery` 把已恢复的字节顺序送入增量 SHA-256。完整 raw 文件、空 raw 文件及清单内按顺序完整覆盖的源文件与输入 SHA-256 核对。manifest 的范围不完整、重叠或乱序时，整文件证据标为 `not_applicable`，逐块校验仍执行。失败保留已生成的统计及失败状态，进一步的块级诊断可以通过原始出现 ID 关联。无需再次解码、读取源文件或将恢复文件写盘。

## 启动方式

配置继续使用原有键及参数。每次运行的 `outputDirectory` 必须指向新的目录。原 CLI 默认使用 `benchmark`，显式模式可附在两个入口之后。

```sh
build/release/Finesse --config /path/to/new-sf.conf --instrumentation benchmark
build/release/Finesse --delta-config /path/to/new-delta.conf --instrumentation profiling
build/release/Finesse --delta-config /path/to/new-delta.conf --instrumentation off
```

| 模式 | 数据 |
| --- | --- |
| `off` | 关闭第 17 号文档新增的采集与统一导出。保留旧版计时、实验结果文件和所有正确性校验，作为新增框架的增量成本基线。 |
| `benchmark` | 记录阶段调用、时长累计、最小最大、文件聚合、计数和进程资源。SF 主循环按整轮计时，调用次数由已完成轮次的提取数量得到。 |
| `profiling` | 另收集块、候选检索、逐次编码和恢复解码明细。SF 轮次内增加逐块计时，适合诊断。 |

## 时间边界

统一采集用 `steady_clock`，内部记录带小数的微秒。新导出同时提供 µs 和 ms，后者为 µs 除以 1000。旧字段如 `cdcElapsedNs`、`encodeNs` 和 `verifyNs` 仍按纳秒解释。失败的作用域也结束计时。

| 阶段名 | 埋点和范围 |
| --- | --- |
| `input_load` / `file_processing` | 输入加载入口 / raw 单文件从读取前到分块、去重和释放源缓冲完成。 |
| `file_read` / `source_sha256` | 新源文件的实际读取 / 已读字节的 SHA-256。 |
| `cdc_total` | 每个 raw 文件调用 `chunkBoundaries`，包含检查和切点数组构建。manifest 无此阶段。 |
| `dedup_total` | `storeChunk` 全程。嵌套 `fingerprint`、`sha1_lookup`、`unique_payload_write` 和 `sha1_insert`。 |
| `sf_round` / `feature_extraction` | SF 正式轮或预热轮的提取与结果写入 / 单块完整签名提取。SF 主循环仅 Profiling 有逐块行，Delta 构建与 SF 诊断两种开启模式均有。 |
| `sf_index_initialize` / `sf_lookup` / `sf_insert` | 建索引、查 FirstFit、插签名。候选匹配在 `sf_lookup` 返回时记录，搜索与匹配是同一次操作。 |
| `encoded_pool_build` | 从初始化算法池和索引到完成所有唯一块。嵌套 SF、索引、`target_sha256`、`chunk_sha256_verify`、编码、回放和 payload 写入。 |
| `delta_encode` / `roundtrip_verify` | 调用 Xdelta 编码封装的完整区间 / 立即解码并与目标原文逐字节比较。基块 SHA-256 在编码区间外。 |
| `encoded_payload_write` | 最终 Raw/Delta 记录追加及 Raw 拷贝，属于内存写入。 |
| `chunk_recovery` | 每次出现的恢复、完整性校验和旧版审计行构造。嵌套 `raw_read` 或 `delta_decode`、`chunk_sha256_verify`。 |
| `file_reassembly` / `file_sha256_update` / `file_sha256_verify` | 跟随块恢复进行有序累积、更新整文件摘要、最终核对。前两者嵌套。旧 `restorationSweepNs` 排除增量摘要区间与最终文件比较。 |
| `sf_output_digest` / `artifact_write` | 轮次签名结果摘要 / 原有产物写入。 |

阶段累计时间会重叠。整次命令的墙钟时间由开销脚本测量，包含启动、导出和退出。`summary.json` 与 `system_metrics.csv` 中的墙钟从 Run 初始化到主要统一统计文件写出，包含原有产物写入。`processing_wall_time_us/ms` 单列统一导出前的运行窗口。最终系统表、汇总和退出时间以外部进程计时为准。吞吐分子是输入范围字节，分母为此运行窗口，每组比较运行中会执行两个算法。它与单算法五轮复查的吞吐口径不同。

## 导出字段及关联

所有统一结果使用 schema_version 1。顺序 ID 从 0 开始。以 `experiment_id` 关联同一运行，`file_id` 对应 source-checksums.csv 的行序，`chunk_id` 对应出现记录。`stored_id` 对应唯一原文和两份编码池。`first_chunk_id` 是唯一块第一次出现的逻辑 ID。阶段/操作内的空 ID 表示该层没有适用实体。未执行的操作没有阶段行，已记录的 0 表示实际零值。未知数值在 CSV 中留空，JSON 比率不可定义时为 null。

| 文件 | 字段与口径 |
| --- | --- |
| `experiment_config.json` | 实验 ID、模式、起始 UTC、入口类型、提交及源码摘要、编译器/选项、平台、可读取的 CPU 和物理内存、线程数、完整解析后的外层和输入配置。 |
| `summary.json` | 完成状态/原因、结束 UTC、源文件总字节、输入范围字节、块计数、墙钟与吞吐、RSS、阶段累计、编码字节及 DCR、整文件校验次数及通过率、统一产物清单。 |
| `stage_timing.csv` | 算法、阶段、操作名、调用数、失败数、各阶段 input/output_bytes、累计 µs/ms、平均与极值 µs、样本数和 P50/P95/P99。Profiling 用每组最早 4096 个观测的最近秩分位数，此样本有顺序偏差。总量计算覆盖所有调用。嵌套阶段的字节不能求和。 |
| `file_metrics.csv` / `file_stage_timing.csv` | 路径、后缀、字节、源 SHA-256、出现数、首次唯一及重复次数。时间按 file_id、算法、阶段、操作汇总。 |
| `chunk_metrics.csv` | Profiling 的出现 ID、文件 ID、偏移、字节、唯一 ID、SHA-1、重复标记。实际 SF 参与记录关联 `feature_extraction` 操作。 |
| `chunk_storage_metrics.csv` / `file_storage_metrics.csv` | Profiling 的最终唯一块 Raw/Delta、基块 ID 和 payload 字节。文件表按首次出现归属汇总，避免跨文件重复计费。 |
| `operation_metrics.csv` | Profiling 中选中的操作、运行内 operation_id、文件/块关联、耗时 µs/ms、结果、输入/输出字节、关联候选。`related_chunk_id` 在 Delta 构建/恢复中为唯一 ID，在 SF 诊断中为出现 ID。 |
| `index_metrics.csv` | SHA-1 与 SF 索引各自的查询/命中/未中/插入计数、最后条目数量、查询/插入/初始化时间。当前无删除。 |
| `storage_metrics.csv` | 两算法分别给出唯一原文、Raw、Delta、总 payload、块数、DCR 和节省率。DCR 为原文 ÷ payload，节省率为原文与 payload 之差 ÷ 原文。 |
| `recovery_metrics.csv` / `file-restoration.csv` | 算法、文件 ID、恢复字节、原始及恢复 SHA-256、结果。后一份是跨模式的正确性证据，已接入独立审计脚本。 |
| `system_metrics.csv` | 墙钟快照、进程累计 CPU 时间差、CPU 时间 ÷ 墙钟的单核百分比、进程峰值 RSS、线程数。 |
| `algorithm_metrics.jsonl` | 算法、实体类型/阶段、指标名及类型、单位和值、定义。当前扩展包含 FastCDC 切点、SF 槽位和 FirstFit 命中，以及 Raw 读取和 Delta 解码次数。 |

缓存、索引分配器真实字节、内存物理占用拆分及磁盘 payload 物理占用当前均无可信测量。现有算法没有缓存操作或持久化 payload，因此无相应数据文件。结果导出只记录实际逻辑 payload 和进程 RSS。

## 接入新算法

在入口先创建 `metrics::Run`，在实际调用点设置 `Scope("NewAlgorithm", "build")`。文件 ID 与块 ID 用内层 Scope 提供。阶段名、算法名和结果标签使用生命周期覆盖导出的字符串。创建定时器后让它随作用域结束，或用 `finish` 精确停止并补充结果。

```cpp
const metrics::Scope algorithm("NewAlgorithm", "build");
metrics::registerCounter("blocks_selected", {"count", "成功选择的目标块数"});
const metrics::Scope block(fileId, chunkId);
metrics::Timer search("candidate_search", inputBytes, true);
const auto candidate = searchCandidate(blockView);
search.finish(candidate ? "hit" : "miss");
if (candidate) {
    metrics::count("blocks_selected");
}
```

若算法未来使用工作线程，每线程创建独立 Collector，并在该线程用 Activate 绑定。主线程等待结束后再 merge，统一分配明细 ID 后导出；线程的 Stage Time 属于累计工作量。

## 验证与开销

`scripts/benchmark_instrumentation.py` 对相同输入生成新目录配置，轮换 off、benchmark、profiling。默认一次预热和三次正式运行，逐轮记录进程墙钟、CPU、峰值 RSS 和产物总字节。开销为 `(模式平均墙钟 − off 平均墙钟) / off 平均墙钟`。逐轮检查去重结果、候选结果、最终编码表示和恢复摘要，或 SF 结果摘要的一致性。

## 平台资源接口

新增采集和导出采用 C++20。系统接口集中于 `Platform.cpp`。Windows 使用进程 CPU 时间及峰值工作集，macOS/Linux 使用 `getrusage`。macOS RSS 原生单位为字节，Linux 的 KiB 转为字节。无法取得的观测使用空字段。开销脚本使用 POSIX `wait4` 或 Windows 进程资源 API。现有实验及依赖的历史平台约束需要分别处理。

### macOS 上的实跑对照

FastCDC DataSet_4，Apple M5、Release，每模式三次正式运行。表中时间为包含导出的进程平均墙钟，括号是相对 off 的变化。

| 入口 | off | Benchmark | Profiling |
| --- | ---: | ---: | ---: |
| SF | 0.972 s | 1.052 s（+8.16%） | 1.014 s（+4.33%） |
| Delta | 1.248 s | 1.211 s（−3.03%） | 1.195 s（−4.26%） |

三种模式的算法结果一致。Delta 的负差值是这组三轮对照中的观测，不能据此判断采集框架带来加速。Profiling 相比 off 增加逐操作数据，SF 平均产物从 4.67 MB 增至 15.00 MB，Delta 从 10.78 MB 增至 15.88 MB。

- [SF 逐轮开销](../results/instrumentation-20261124/sf/overhead-rounds.csv) 与 [汇总](../results/instrumentation-20261124/sf/overhead-summary.csv)
- [Delta 逐轮开销](../results/instrumentation-20261124/delta/overhead-rounds.csv) 与 [汇总](../results/instrumentation-20261124/delta/overhead-summary.csv)
- [Finesse、N-transform 和 FastCDC 的完整 Delta Profiling 示例](../results/instrumentation-20261124/delta/run-1-profiling/)
- [两种 SF 算法的 Profiling 示例](../results/instrumentation-20261124/sf/run-1-profiling/)

本机 Release 和 Debug（ASan/UBSan）各 7/7。独立审计通过本次 Profiling 的 Delta 输出，两个算法各核验 24,931 次块恢复和八个文件的 SHA-256。Windows 和 Linux 的新平台分支尚未在对应系统上编译或实跑。
