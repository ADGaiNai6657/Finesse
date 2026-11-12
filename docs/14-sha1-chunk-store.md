# D-21：SHA-1 精确查重与 ChunkStore 接入（已批准）

用户明确要求实现 SHA-1 的精确 chunk 重复性检查，并实际接入存储，实现完全相同的 chunk 只存一份；完成后更新 Review 导读与相关文档。此指令授权启用此前 D-16 未启用的精确去重。

## 核心实现

`src/Hashing/Hash` 使用 OpenSSL EVP 计算完整 SHA-1（20 字节）；`src/Storage/ChunkStore` 采用 SHA-1 多值索引查候选，再比较长度与全部字节。相同摘要而内容不同的块分别保留。唯一块拥有独立字节副本，重复出现返回相同 storedId，引用同一 payload，不重复复制内容。

Store 是实验独占状态，不使用上游可变全局变量。storedId 按首次出现顺序生成，与原 chunkId 分开；原始出现记录保留来源路径、偏移、长度与 storedId。所有长度都参与精确去重，SF 仍按 D-04 排除小于 12 的块。

核心测试涵盖 SHA-1 已知答案、二进制内容、强制相同摘要下不同内容、复用同一 backing memory、源缓冲释放后的所有权、追加时引用稳定、计数、状态隔离与 reset。

## 已确认的接入口径

1. **存储生命周期**：用户选择“采用内存唯一块池（推荐）”。Store 在一次实验中跨文件保留，本次结束后释放。raw 模式逐文件完成 CDC 和入池后释放源缓冲；manifest 模式因可任意回访文件，在加载与摘要校验完成后释放暂存缓冲。返回的 `ExperimentInput` 不再拥有原文件 payload；测量和诊断从 Store 取视图。磁盘持久化、进程重启重载仍在 D-13 范围。
2. **SF 输入集合**：用户选择“默认唯一块，保留 all 入口（推荐）”。当前四组数据和验收配置显式设 `sfInput=unique`；`all` 是原全部出现集合的对照入口。配置增加必填键 `sfInput=unique|all`，不能省略后隐式猜测。两种模式都真实去重存储，只改变测量遍历的引用集合。

具体可审阅的接入方案：raw/manifest 加载 → SHA-1 精确查重并绑定 storedId → 释放原文件 payload → 从 ChunkStore 构造相同输入集合 → 预热/五轮完整 SF 测量 → 独立候选诊断。CDC、去重和 SF 分开计时。

新增产物为 `stored-chunks.csv`（每个唯一块的 ID、首次 chunkId、长度、SHA-1、引用次数）、`chunk-references.csv`（每次出现到 storedId 的映射）、`deduplication.csv`（原始/唯一/重复计数与字节数、去重时间）。它们是存储元数据和统计；内存方案不会将 CSV 当作磁盘 payload 存储。

测量记录继续保留原输入与短块统计，另明确记录 `sfInput`、所选集合的块数/字节数/短块数；`processedBytes` 按所选集合的有效字节数乘 iterations。存储 payload 字节为 uniqueBytes，元数据和索引内存单独解释，不冒充完整系统 storedBytes 或 Delta 指标。

## 数据与所有权契约

- `ChunkStore` 持有 `deque<StoredChunk>` 和完整 20 字节 SHA-1 多值索引。`StoredChunk.bytes` 自持有内容；重复出现仅增加引用次数和统计，不复制 payload。
- `storeChunk` 返回 `storedId,isDuplicate`。ID 是本次池内从 0 起递增的标识，不能与输入的任意 `chunkId` 混用。
- `InputChunk` 保留原 `chunkId,fileIndex,offset,length`，新增 `storedId,isDuplicate`；`uniqueChunkPositions` 记录首次出现的行位置。`SourceFile` 仅保留路径、原长度、SHA-256。
- `inputChunkView` 从唯一池解析引用并核对长度；视图在 Store 存活且未 reset 时有效。普通追加不会移动旧 payload。禁止外部修改池内容或中途替换哈希函数。
- `findDuplicate` 只读，不改变统计；`resetStore` 清空索引、payload、计数，全部旧视图失效。每个新实验使用新 Store，不在预热或两算法之间重建/累加 Store。
- 精确去重涵盖 SF 不支持的短块。raw 空文件没有块；manifest 明确给出的零长度出现允许指向唯一空块。

当前方案不需要为每次出现复制原始内容；读取某次出现只需用其 storedId 获得同一原文。重放清单仍依赖源文件，本进程内存池不等于磁盘备份。

## 统计与文件格式

| 字段/文件 | 精确定义 |
| --- | --- |
| `inputBytes/chunkCount`、`excludedBytes/excludedChunks` | 所有原始出现的总量及其中短块量，保留旧口径 |
| `uniqueBytes/uniqueChunks` | 实际唯一池的 payload 字节数和块数，包括唯一短块 |
| `duplicateBytes/duplicateChunks` | 每次重复出现累计的长度和次数 |
| `storedPayloadBytes` | 等于 uniqueBytes，不包含对象、索引、引用元数据或源文件加载时的暂存 |
| `sfInputBytes/sfInputChunks` | 所选 unique 或 all 集合在排除短块前的总量 |
| `sfExcludedBytes/sfExcludedChunks` | 所选集合中的短块总量 |
| `processedBytes` | `(sfInputBytes-sfExcludedBytes)*iterations` |
| `dedupRatio` | inputBytes/uniqueBytes；分母为零时为空 |
| `dedupSavingRatio` | duplicateBytes/inputBytes；分母为零时为空 |
| `dedupElapsedNs` | 各次 storeChunk 调用耗时之和，含 SHA-1、候选确认、唯一插入，不含 SF、文件 I/O 与出现记录导出 |
| `stored-chunks.csv` | `storedId,firstChunkId,length,sha1,referenceCount`，一行一个唯一块 |
| `chunk-references.csv` | `chunkId,file,offset,length,storedId,firstChunkId,isDuplicate`，一行一次原始出现 |
| `deduplication.csv` | input/unique/duplicate 的块数和字节数、payload 字节、两种比例、去重耗时及内存范围说明 |

恒等式为 inputChunks=uniqueChunks+duplicateChunks、inputBytes=uniqueBytes+duplicateBytes、sum(referenceCount)=inputChunks。唯一块顺序与首次出现顺序相同。诊断与 SF 使用同一所选集合：unique 不把精确重复再次送入相似索引，all 保留每次出现。

measurement schema 更新为 2：保留原 13 列，追加 `sfInput,sfInputBytes,sfInputChunks,sfExcludedBytes,sfExcludedChunks`；summary 追加 sfInput。run.txt 标明 storageEnabled、contentHash、duplicateConfirmation、sourcePayloadRetained、所选 SF 集合和存储计数。

## 测试与来源

`ChunkStoreTests.cpp` 检查已知 SHA-1、强制摘要碰撞、相同内容同一地址、所有权、追加稳定性、查询不计数、reset 与溢出失败；`DeduplicationTests.py` 通过真实 CLI 独立按字节构造唯一集合，检查跨文件与短块去重、SHA-1、引用映射、原文恢复、unique/all 的测量和重放。原有算法与实验测试继续运行。

Hash/ChunkStore 参考固定 FastCDC 提交 `be5816e8e2e8548738db3e7dd54aa323056df402` 的 EVP SHA-1、候选桶与逐字节确认语义，本项目改为显式 Store 所有权、存储 ID、错误检查与回滚。它们属于适配实现，不标为原样摘取，详见 [来源记录](10-source-provenance.md)。

## D-24 的后续消费与生命周期

本页 ChunkStore 在 SF 模式中仍是最终原文唯一池；在新的 Delta 模式中是构建输入/字节比较 oracle，两个独立 EncodedStore 完成后将其销毁。最终每个唯一 storedId 仅保留该算法的 Raw 或 Delta，全部出现沿用该 ID 引用；Delta 的 baseId 仅指向此前 Raw。原文释放后恢复仅接收 EncodedStore/ID，不访问 ChunkStore 或文件。

因此 Delta 运行的 deduplication.csv / stored-chunks.csv 描述构建输入唯一集合，不是最终 payload 大小；最终表示和 DCR 必须读取 compression-summary.csv / encoded-chunks.csv。精确去重算法、碰撞确认、唯一顺序及 SF all 对照入口未变，见 [15](15-xdelta-pipeline-proposal.md)。磁盘持久化/重载仍未批准。
