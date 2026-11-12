# D-22～D-24：实际 Xdelta 编解码与内存 Raw/Delta 存储（已批准）

用户已指定继续按文档和论文构建真实的 `A = decode(B, Delta)`，并使用 [jmacd/xdelta](https://github.com/jmacd/xdelta) 的现有代码。用户已明确回复“批准 D-22～D-24 推荐整套方案”。以下为批准后实施契约；原提案文件名保留以追溯审批。

## 核实依据

- Finesse 论文 §5.1，论文集 p.125 / PDF 第 6 页：先精确去重，再对非重复块做相似检测、读基块和 Delta 编码；FirstFit 取首个匹配；LRU 基块缓存 400MB；使用 Xdelta。原文没有给出 Xdelta 版本、编码级别、Delta 链或负收益处理规则。
- D-09 已确定跨槽 SF 匹配，当前槽升序，同键保留最早插入；D-21 已确定本进程内存唯一块池，默认 unique 集合，磁盘持久化仍未批准。
- 上游当前 main 固定提交 `5d936d2c30f78729375c3ae070248a05bef27bd2`，源码标记 3.2.2，含 release 后的空指针算术/空解码段修复；正式 `v3.2.2` 标签另指向 `5f8f5a553181c139d91b0289c009e5c276c5318c`。二者不是同一提交。
- 上游提供 `xd3_encode_memory`、`xd3_decode_memory` 现成 C API，以及可嵌入静态库的 CMake target；main/3.2.x 为 Apache 2.0，保留原许可证与来源清单。
- 编解码库可以不依赖 liblzma/BLAKE3；这些分别对应可选二级压缩和 CLI armor 功能。

## D-22：固定的编码器配置

直接收录上述 main 固定提交的必要源码、上游 CMake/配置模板和许可证至 `third_party/xdelta`，记录逐文件 SHA-256；不在每次构建时下载浮动 main，不重新实现 Delta 算法。本次批准的是上述固定 main 提交；正式标签不是同一版本快照。

编译上游 C99 静态核心库，通过内存 API 调用，C++ 包装返回拥有 Delta 字节的结果；不依赖本机 `xdelta3` 可执行文件。编码级别固定 6（`XD3_COMPLEVEL_6`），启用 `XD3_ADLER32`，不启用二级压缩；关闭 CLI armor、liblzma/BLAKE3 拉取和上游 CLI 的默认构建。适配层检查输入、输出容量与错误码，输出空间不足时扩大缓冲后重试，其他错误使本次实验失败。

编码与解码接口方向固定为：`delta=encode(base=B,target=A)`；`restored=decode(base=B,delta)`。每次都核对还原长度与原始 A 的全部字节，不能把“产生了 Delta”当成成功。

## D-23：单层 Delta、Raw 基块与正收益回退

第一版只允许 Raw 记录成为基块，最大 Delta 深度为 1。候选仍按 D-09 的跨槽/槽位优先/最早插入规则查找，但索引只插入最终存为 Raw 的合格块。**这是对基块资格的明确补充，会使实际压缩候选与原有全量 SF 诊断有所不同。** 两种算法使用同一资格规则。

处理顺序：

1. SHA-1＋长度/字节确认重复：只保存出现引用，不再编码一次。
2. 首次出现的唯一块：计算 SF 并查以前的合格 Raw 基块；短块没有 SF，直接 Raw。
3. 未匹配：存 Raw，并按资格加入候选索引。
4. 匹配：真实调用 Xdelta，记录尝试的 Delta 长度，立即解码并与 A 逐字节核对。
5. 仅当 `deltaBytes < originalBytes` 时存 Delta（baseId＋Delta 字节）；否则存 Raw，并记录无收益回退。编码/解码错误不能伪装成正常回退。
6. Delta 记录不持有原始 A 的第二份 payload，也不作为下一次编码的基块。

此方案避免链、循环和隐式原文缓存。若需要链，必须另定最大深度、基块资格和递归恢复/缓存策略；本提案不自行选择链深。

接受阈值按 payload 比较；内存记录、ID、索引等元数据另列，不将 payload 节省冒充完整磁盘存储节省。Raw 和 Delta 记录都保存还原长度与目标完整性摘要，读取时校验。

## D-24：沿用已批准的内存存储范围

新增实际承载内容的 EncodedStore：每个唯一 ID 仅持有 Raw 字节或 VCDIFF Delta 字节，出现记录仍映射到唯一 ID。两算法各自建立独立 EncodedStore，避免彼此候选和表示污染。

当前 ChunkStore 作为构建输入和逐字节核验的原文来源；构建结束后释放原始池，验证 EncodedStore 在仅依赖自己的 Raw 基块和 Delta 时仍能还原全部逻辑 chunk。解码不能偷偷借用原文池或源文件。构建期间的原文/输出暂存与最终编码 payload 分开统计。

继续采用内存生命周期，进程退出即释放；磁盘容器、跨进程重载、400MB 磁盘基块 LRU 与并行流水线不在本版范围，因此只称“可恢复内存 Delta 管线”，不称原论文磁盘系统复现。

新增独立入口：`Finesse --delta-config <file>`，保留现有 SF 微基准入口。Delta 配置中的 inputConfig 相对自身目录引用现有数据配置，导入其数据、CDC 和 Gear 参数，要求 sfInput=unique；不执行其 SF 微基准，也不使用其旧结果目录。所有新键必填，未知/重复键拒绝。

正式示例见 [配置 4](../configs/fastcdc-dataset-4-delta.conf)，另有 1～3 组对应配置，使用新的 `*-delta-release-v1` 输出目录。每组每算法构建/核验一次，用于验证存储与压缩质量；SF 五轮测速继续由原入口执行。单次构造时间、编码/解码时间分别记录，不据此报告稳定系统吞吐倍数。

## 产物与指标

- run.txt：上游版本/提交/源码摘要、全部编码与基块策略、构建和输入摘要、验证状态。
- encoded-chunks.csv：唯一 ID、首次 chunkId、Raw/Delta 标签、可选 baseId、原长度、实际 payload 长度、还原摘要。
- delta-attempts.csv：每次候选、尝试编码大小、原长度、接受/回退及原因、编码/核验耗时。
- occurrences.csv：全部原始出现到唯一编码记录的映射。
- compression-summary.csv：唯一原始字节、最终 Raw＋Delta payload、匹配/接受/回退计数、最大深度、恢复验证计数。

DCR 按“全部唯一块原始字节 / 最终 Raw＋Delta payload”计算，包含未匹配和回退 Raw。匹配尝试的 residual/saving 比例与最终实际存储比例分别输出，均按字节加权；没有匹配或分母为零时留空并说明原因。不将其中一项直接命名为论文有歧义的 DCE。

上述 CSV 保存元信息与统计，实际 Raw/Delta payload 位于内存 EncodedStore；它们不是可跨进程重载的磁盘容器。

## 验收与文档同步

必须验证真实 Xdelta 的局部修改、头/中/尾插删、不同长度、零字节/空内容、无收益 Raw 回退、错误基块、损坏/截断 Delta、非法记录引用。独立测试释放原文池之后仍能恢复；Delta 记录不保留原 A；不得引用自己或未来块；精确重复不复制 payload。

接入后同步结构、接口、实验协议、配置、来源、审批记录、Review 导读和 AGENT.md。Debug ASan/UBSan 与 Release 测试通过后，用当前 FastCDC 四组数据分别构建两算法的编码存储并核验全部恢复内容。保留用户已有改动（当前 Types.cpp 的 constexpr 改动不回退）。

## 实际实现与阅读入口

- [DeltaCodec](../src/Compression/DeltaCodec/DeltaCodec.cpp)：调用原上游内存 API，参数为 `XD3_COMPLEVEL_6 | XD3_ADLER32`。不设置 `XD3_SEC_TYPE` 即无二级压缩；`SEC_NOALL` 不能脱离 compressor 类型独立使用。C 库采用上游已有 memcpy 读取分支，避免对齐未定义行为；未修改上游算法文件。
- [EncodedStore](../src/Storage/EncodedStore/EncodedStore.cpp)：`appendEncodedChunk` 做真实编码/逐字节回放/表示选择；`buildEncodedStore` 按首次出现顺序调用两阶段索引；`restoreEncodedChunk` 仅接收编码池和 ID，检查基块资格、长度和 SHA-256。
- [Pipeline](../src/Runtime/Pipeline/Pipeline.cpp)：`queryCandidate` 只查询；`insertSignature` 仅在最终 Raw 且长度≥12 时调用。旧 `queryThenInsert` 保持 SF 诊断行为。
- [DeltaExperiment](../src/Runtime/Experiment/DeltaExperiment.cpp)：严格解析 16 个必填键、单次构造两池、原文析构后遍历全部出现并恢复，成功才生成压缩汇总。
- [单元测试](../tests/DeltaStoreTests.cpp)、[CLI 测试](../tests/DeltaExperimentTests.py)、[独立核验脚本](../scripts/verify_delta_results.py)。独立脚本对照源范围摘要和 CSV 证据，不从 CSV 加载 Raw/Delta payload。

`EncodedChunk` 仅含 id、firstChunkId、kind、可选 baseId、originalLength、restoredSha256、payload；没有备用原文或原文指针。encoded id 与构建输入 storedId 一一对应，和任意外部 chunkId 分开。`DeltaAttempt.candidate.chunkId` 在此入口表示 encoded id；旧诊断则使用出现 chunkId，不可混用两类产物。

`appendEncodedChunk` 是一次唯一内容的低层追加操作，不自带精确去重；仅由已完成 SHA-1/字节确认的输入池构建器调用。重复出现不调用追加函数。查询索引不修改状态，最终表示确定后才插入 Raw。

构建期间同时存在原文唯一池和两算法编码池，另外有编码/解码临时缓冲；构造结束的作用域显式销毁原文池，之后恢复只依赖两份编码池。最终 payload 统计分别计算，不把两个算法的存储量相加；uniqueOriginalBytes 是输入量，不能当作最终驻留内存。

## 配置与运行

```sh
cmake --build build/release -j 4
ctest --test-dir build/release --output-on-failure
build/release/Finesse --delta-config configs/fastcdc-dataset-1-delta.conf
build/release/Finesse --delta-config configs/fastcdc-dataset-2-delta.conf
build/release/Finesse --delta-config configs/fastcdc-dataset-3-delta.conf
build/release/Finesse --delta-config configs/fastcdc-dataset-4-delta.conf
python3 scripts/verify_delta_results.py results/fastcdc-dataset-1-delta-release-v1 results/fastcdc-dataset-2-delta-release-v1 results/fastcdc-dataset-3-delta-release-v1 results/fastcdc-dataset-4-delta-release-v1
```

配置路径相对配置文件目录，与启动 cwd 无关。inputConfig 引用原 SF 配置，其全部键仍须合法；这里只使用输入、CDC/Gear 和数据说明，不使用其 iterations、旧 runId/输出、diagnostics，不执行预热与五轮 SF。两种入口的结果目录互相独立，既有目录拒绝覆盖。新目录中的失败记录 status=failed，并移除 compression-summary.csv。

空 raw 文件仍产生零块，记录源摘要；若整个数据集为空，存储构造与零次恢复合法，比例缺失并记录 zero denominator。这不同于要求有效样本才能输出性能的 SF 微基准。

## CSV 与指标核对

所有逐块 CSV 含 algorithm 列（两算法共用的 occurrences 除外）。encoded-chunks 含 `storedId,firstChunkId,kind,baseId,originalBytes,payloadBytes,restoredSha256`；delta-attempts 含 `storedId,baseId,querySlot,historicalSlot,originalBytes,deltaBytes,accepted,reason,encodeNs,verifyNs`，reason 为 strictly-smaller 或 no-payload-gain。

新增 `restoration-verification.csv`：每算法每原始出现一行，`chunkId,storedId,bytes,restoredSha256,status`，摘要由原文释放后实际恢复的字节重新计算。occurrences 含 `chunkId,storedId,fileIndex,offset,length,isDuplicate`，fileIndex 对应 source-checksums 行序；manifest 和 chunk-references 同时保留路径。输入出处、SHA-1 唯一元信息与 deduplication 仍按 D-18/D-21 导出；`deduplication.storedPayloadBytes` 表示构造输入唯一原文池，最终表示量请看 compression-summary。

| 汇总字段 | 定义 |
| --- | --- |
| uniqueChunks / uniqueOriginalBytes | 全部唯一原文数量/字节，包含短块 |
| finalPayloadBytes | 此算法 Raw＋接受的 Delta payload 总字节 |
| rawChunks / deltaChunks | 最终表示计数；两者之和等于 uniqueChunks |
| matchedAttempts / rawFallbacks | 全部候选尝试数 / 无正收益回退数；attempts=deltaChunks+fallbacks |
| attemptedOriginalBytes / attemptedDeltaBytes | 所有匹配目标原始字节和尝试编码字节，包含失败收益 |
| acceptedOriginalBytes / acceptedDeltaBytes | 仅实际接受目标的两种字节和 |
| dcr | uniqueOriginalBytes / finalPayloadBytes |
| storedResidualRatio / storedSavingRatio | finalPayloadBytes / uniqueOriginalBytes，及其互补比例 |
| attemptedResidualRatio / attemptedSavingRatio | attemptedDeltaBytes / attemptedOriginalBytes，及其互补比例；saving 可为负 |
| acceptedResidualRatio / acceptedSavingRatio | acceptedDeltaBytes / acceptedOriginalBytes，及其互补比例 |
| maxDeltaDepth | 存在 Delta 时为 1，否则 0 |
| verifiedOccurrences / verifiedBytes | 原文池释放后的实际恢复出现数/字节数，包含重复 |
| buildNs | 此算法一次构造，含 SF/索引/目标摘要/真实编码/逐字节回放/最终 payload 分配；不含加载/CDC/去重/导出 |
| encodeNs / roundtripVerifyNs | 所有候选真实编码 / 立即解码并完整字节比较的累计耗时 |
| restorationSweepNs | 原文释放后的全部出现恢复、完整性摘要与验证行内存构造；不含源读取和 CSV 写盘 |
| pipelineRuns | 固定 1，质量与恢复验收，不是五轮系统吞吐 |

零分母字段为空，在 run.txt 说明原因。字节比例为加权总体比，不是各块比例平均。不估算含元数据/分配器/索引的存储字节，也不将最终 payload 当作进程 RSS。输入/编码记录/引用数量与 payload 分列；元数据是 CSV 证据而非持久化内容。

run.txt 记录固定上游提交/版本/源码清单摘要、所有策略、输入配置/描述文件/完整源摘要、构建 source-files 快照与参数、计时范围和释放后验证状态；关键 CSV 再保存 SHA-256。xdelta-source-files.csv 是构建时嵌入清单，无运行时外部源码依赖。

实跑与验收记录见 [16-delta-validation.md](16-delta-validation.md)。磁盘持久化、跨进程恢复、400MB LRU 和并行流水线仍未实现，相关取舍须另经人类批准。
