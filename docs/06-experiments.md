# 实验协议、结果格式与验收

## 实验分层

`[已批准 D-01、D-21～D-24]` SF 微基准与可恢复内存 Delta 质量实验分开运行；磁盘完整系统仍延后。SF 协议保持 D-11/D-12/D-16/D-21，新增 Delta 由 D-22～D-24 规定。

| 实验 | 首期地位 | 输出 | 不可据此推出 |
| --- | --- | --- | --- |
| 算法正确性 | 必需 | 已知答案、参考实现一致性 | 真实数据集压缩收益 |
| SF 计算微基准 | 必需 | 完整 SF 计算 bytes/s、两算法速度比 | 完整系统吞吐量 |
| 相似性诊断 | 已批准跨槽诊断 | 相同 SF 命中数、槽位、候选 ID | 无真值条件下的精确率/召回率 |
| 精确去重与存储 | D-21 已接入 | 唯一/重复计数、payload 字节、引用映射、去重耗时 | 不包含磁盘与元数据的完整存储收益 |
| Delta 压缩质量 | D-22～D-24 已接入 | 真实编码的 payload DCR、尝试/接受/最终剩余与节省比例 | 不含元数据的 payload 不能当作磁盘空间或 RSS |
| 系统吞吐量 | 后续 | 从原始输入到存储完成的端到端速度 | 不能只用 SF 时间代替 |

## 输入协议

`[已批准 D-11、D-21]` 原文件经 FastCDC 生成 chunk，再经 SHA-1＋长度/逐字节确认绑定唯一 storedId；两算法读取同一池内的同一所选序列。CDC 和去重均在 SF 计时外。清单保留所有出现，仍为 `chunkId,file,offset,length`，UTF-8 CSV、有表头，路径相对清单目录，文件名标准转义。源文件及清单 SHA-256 用于复现完整性，独立于内容去重 SHA-1。

加载时检查 ID 唯一、非负范围、整数溢出、`offset+length` 不超过文件长度；不接受读取不足后继续实验。清单允许为同一内容保留不同 chunk ID，以测试重复输入；是否已去重必须在运行记录中标明。

没有真实数据时，只能运行明确标为 `synthetic` 的正确性样例或微基准。FastCDC CDC 已由用户批准，报告必须明确这一变更；不把任意固定 8KiB 切块称为 CDC，也不把当前变体称为原论文 Rabin 配置。

合成覆盖：全零、全 0xff、递增字节、固定可重现伪随机字节、局部替换、头/中/尾插入删除、大小不同但共享大片内容的配对。合成修改比例不是压缩收益或相似性真值；D-17 已批准验收样例，生成器、规模与种子记录在样例配置及 dataset.txt 中。

## 已批准的首期输入配置

[已批准 D-16] CDC 使用 FastCDC `be5816e`，expected=8192 bytes、min=2048 bytes、max=65536 bytes、NC=0；按来源构造函数，NormalSize=8192，MaskA/MaskS/MaskL 均为 `0x0000d90303530000`。文件路径排序后逐文件分块，不跨文件延续分块状态。

所有原始出现都进入 SHA-1 精确去重与引用记录，包括短块；相同内容只在内存池保存一份。D-21 默认配置显式 `sfInput=unique`，仅首次出现的唯一块进入 SF；`all` 从同一池按所有出现遍历作对照。两算法共同排除所选集合中长度小于 12 的块。原始与所选集合的短块分别统计，禁止混用分母。数据继续使用 D-20 的 FastCDC 四组（第四组 synthetic），相对路径与筛选见 12。

## SF 微基准计时协议

`[论文 P-11]` 原论文每个实验运行五次。`[已批准 D-12]` 首期采用一次不计入统计的预热和五次正式测量，单线程、同一进程内轮换算法先后顺序。

1. 计时外加载、校验并精确去重，释放加载缓冲；准备同一所选集合的 Store 视图、参数和输出缓冲。当前唯一池驻留内存，尚无分批模式；需要超出内存的运行时另明确分批方案，保持两算法相同集合。
2. 以 `std::chrono::steady_clock` 记录完整 SF 提取循环，包括 Gear 扫描、feature 提取、分组、SF 哈希和结果保存；不包括文件加载、SHA-1、精确查重、Store 插入、诊断索引、打印或 CSV。
3. 输出保存到预分配缓冲区，在计时结束后消费并计算摘要，防止编译器消除计算；该保存成本两算法一致。记录计时区间定义，不能让两算法使用不同测量口径。
4. 记录每次耗时和有效字节数，计算逐次速度、均值、标准差。数据太小导致计时不稳定时扩大批次或迭代次数，并记录真实处理字节数，不能挑选最快一次。
5. 不并发运行两算法争抢 CPU。记录 CPU、架构、OS、编译器、Release 选项、线程数、可用内存与计时模式；不自行改变系统电源或绑核设置。

算法速度 `sfBytesPerSecond = processedBytes / elapsedSeconds`，MB/s 采用 `10^6` 字节并写清单位。两算法使用相同有效字节数；主要速度比定义为 `meanBaselineSeconds / meanFinesseSeconds`。不把不同数据量、不同排除短块集合或不同优化级别的结果直接相除。

## 去重与实际内存 Delta 指标

D-21 的 `deduplication.csv` 已报告原始、唯一、重复的块数和字节，以及 `storedPayloadBytes=uniqueBytes`、dedupRatio、dedupSavingRatio 和 dedupElapsedNs。Store 只计算唯一 payload，不把其字节数当作含元数据的磁盘 storedBytes。两种去重比例分别为 total/unique 和 duplicate/total，分母为零时为空。

`[已批准 D-03]` 论文 DCE 存在歧义，同时保留剩余比例和节省比例，不将任一项直接命名为论文 DCE。

`[已批准 D-22～D-24]` 实际计数与精确字段见 [15](15-xdelta-pipeline-proposal.md)，以下为指标含义：

| 名称 | 定义 | 范围与解释 |
| --- | --- | --- |
| `dedupRatio` | 原始输入字节 / 唯一 chunk 原始字节 | 去重收益 |
| `deltaCompressionRatio` | 唯一 chunk 原始总字节 / Delta 阶段后的 payload 总字节 | 包含未匹配块的原始 payload；是否含元数据另列 |
| `matchedResidualRatio` | 所有匹配目标的编码后字节总和 / 这些目标编码前字节总和 | 越低表示剩余越少 |
| `matchedSavingRatio` | `1 - matchedResidualRatio` | 越高表示节省越多；编码膨胀时可为负 |
| 完整 `storedBytes` | 磁盘内容和明确元数据的总量 | 本版未实现磁盘容器，不输出假定值 |

“匹配后尝试编码”和“因收益足够而实际接受编码”必须分别统计，不能只挑成功压缩的样本计算匹配质量。若有回退策略，记录尝试编码长度和最终存储长度。采用字节加权的总体比例，不擅自改成逐块比例的算术平均。分母为零时输出缺失值及原因，不输出 0、1 或无穷来伪装正常结果。

## 配置与输出契约

`[已批准 D-10、D-12；输入来源按更新后的 D-11]` 为减少首期依赖，配置和运行元信息使用 UTF-8 `key=value` 文本；CSV 保存逐次测量与可选逐块诊断。配置拒绝重复键、未知键和缺失必需键；所有改变算法行为的值显式记录，不提供悄悄生效的默认值。

建议的产物：

```text
results/<runId>/
    run.txt             # 版本、平台、配置、数据摘要、审批配置 ID
    parameters.csv      # 实际 m_i、a_i；不是只写一个 seed
    measurements.csv    # 每算法、每次运行的原始数据
    summary.csv         # 从原始数据计算的汇总
    diagnostics.csv     # 可选，不属于计时热路径
```

measurement schema=2 保留原 13 列：`runId,algorithm,profileId,repetition,order,inputBytes,processedBytes,excludedBytes,chunkCount,excludedChunks,elapsedNs,outputDigest,status`；追加 `sfInput,sfInputBytes,sfInputChunks,sfExcludedBytes,sfExcludedChunks`。原 excluded 字段仍描述全部原始出现，`processedBytes=(sfInputBytes-sfExcludedBytes)*iterations`。summary 追加 sfInput。运行另导出 stored-chunks.csv、chunk-references.csv、deduplication.csv，字段见 14。

SF 入口不生成 DCR/DCE 列；Delta 入口另输出真实 payload 指标，不命名为论文 DCE。结果目录存在时拒绝覆盖；失败运行标记 failed，保留原因，不混入正常汇总。

`run.txt` 至少包含：代码 revision（无 Git 时记录源码摘要）、文档版本、dirty 状态或源文件摘要、数据清单及文件摘要、全部算法配置、参数摘要、CPU/OS/编译器、构建选项、线程数、预热/重复次数、时间戳、排除规则、计时边界。具体配置键与样例已按 D-17 批准；D-18 补充空文件的完整源文件摘要记录，见 09。

## 测试矩阵

`[已批准 D-12]`

| 编号 | 内容 | 判据 |
| --- | --- | --- |
| T-01 | Gear 逐字节计算与批量更新；空、1、63、64、65 字节 | 与来源表及模 2^64 递推的独立参考答案一致；分批更新和一次更新等价 |
| T-02 | 子块边界：12、13、14、整除长度与大长度 | 无遗漏、重叠、越界；边界与 D-04 一致 |
| T-03 | 0～11 字节的全部短长度 | 明确返回状态，两算法正式比较的有效集合一致 |
| T-04 | N-transform 乘加模运算 | 独立宽整数计算的参考答案一致，包含最大输入值 |
| T-05 | 图 3 的 12 个 feature | 分组得到 `[F1,F5,F7,F9]`、`[F2,F3,F8,F10]`、`[F0,F4,F6,F11]` |
| T-06 | 分组重复值、全相等值、顺序值 | 排名规则确定，原 features 不被修改 |
| T-07 | 两种算法各自的朴素参考实现对照 | 与各自优化实现逐 feature、逐 SF 一致；不要求两种算法互相相等 |
| T-08 | SF 输入字节向量 | 与批准字节序和位宽一致，不依赖主机端序或结构体 padding |
| T-09 | 重复调用、穿插不同 chunk、独立状态 | 相同输入输出一致，不受前一次调用污染 |
| T-10 | 非法配置、空指针非零长度、损坏清单 | 明确失败，失败数组不进入索引 |
| T-11 | 跨槽匹配诊断 | A.SF0=B.SF2 命中；无自身/未来匹配；重复槽不重复候选；同键选择符合 D-09 |
| T-12 | 实验结果可复核 | 汇总可从原始记录重算，计时覆盖全部 SF 计算 |
| T-13 | SHA-1 精确去重与唯一内存 | 已知答案、二进制零字节、强制同摘要不同内容、重复内容同一地址、引用稳定、reset 与溢出 |
| T-14 | Store 实际接入与 SF 口径 | 跨文件/短块去重、引用恢复、SHA-1、计数恒等式、unique/all 选择与重放一致、SF 预热不污染引用计数 |
| T-15 | 固定真实 Xdelta | 局部编辑、头中尾插删、不同长度、二进制/空内容、错误基块、截断与损坏明确拒绝 |
| T-16 | Raw/Delta 实际存储 | 释放原文后恢复、Delta 不留原 A、无收益 Raw 回退、非法/自身/未来/Delta 基块拒绝、重复只引用 |
| T-17 | Delta CLI 与质量核验 | 严格配置、相对路径、重放确定、旧输出拒绝覆盖、失败状态、空分母，独立按源范围核对恢复摘要/引用图/加权指标 |

T-15/T-16 由 DeltaStoreTests.cpp 执行，T-17 由 DeltaExperimentTests.py 和 verify_delta_results.py 执行；实际状态见 [16](16-delta-validation.md)。

## 首期验收

必需决策已批准；哈希参数与已知答案已冻结；核心正确性和边界测试通过；Debug 检查无越界/未定义行为；Release 下两算法在同一输入上完成五次测量；配置和原始数据足以重算结果。加速倍数只如实报告，不将达到 3.2 倍或 3.5 倍作为强制成功条件。

新增适配验证：同一来源版本、数据和 CDC 配置下逐项比较切点；空文件、短尾块、强制最大块切分和无进展边界必须覆盖。记录 CDC 参数、所有掩码与来源提交。CDC、去重预处理、SF 计算分开计时；若启用去重，按首次出现顺序向两算法提供完全相同的唯一块集合。

## Delta 单次质量与恢复协议

`--delta-config` 按 [15](15-xdelta-pipeline-proposal.md) 的 16 键运行：每组、每算法 pipelineRuns=1，不预热/不重复 SF 五轮。inputConfig 仅导入输入、CDC、Gear 和数据说明，要求 unique；其旧输出、iterations、diagnostics 和微基准不执行。

构建时独立索引只接收最终合格 Raw。每个匹配目标实际编码并立即解码，与原 A 全字节比较，严格正收益才保留 Delta；编码错误中止。两算法完成后销毁原始 ChunkStore，恢复每个逻辑出现，以长度、目标 SHA-256 核对，并导出由恢复字节新计算的摘要。独立脚本再对照源文件范围摘要核对证据与全部统计；该脚本读取源文件，不是跨进程加载 Delta payload。

输入来源、CDC、SHA-1/去重时间另记。buildNs 包含签名/索引/目标摘要/真实编码/即时逐字节验证/分配；encodeNs 与 roundtripVerifyNs 是其中子区间。restorationSweepNs 另含全部出现恢复/完整性检查/验证行内存构造，不含 CSV 写盘；这些单次观察不能称为稳定系统吞吐。

finalPayloadBytes 含全部唯一块的最终 Raw＋Delta；短块、无匹配和回退均计入。所有 attempted 指标含无收益尝试，accepted 仅含保留 Delta，stored 覆盖全集；按字节和计算，零分母为空并说明。构造输入原文池与最终各算法编码 payload 分开列，不将元数据/索引/临时缓冲计为 payload，也不声称已测总内存/完整磁盘存储量。空输入可以完成零块存储验证，不能输出性能结论。

## 通用统计

两入口的统一时间边界、Benchmark/Profiling 模式及 schema 1 输出见 [18](18-experiment-instrumentation.md)。首期纳秒字段沿用原始单位。Profiling 的轮次包含逐块采集成本。
