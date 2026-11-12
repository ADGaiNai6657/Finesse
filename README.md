# Finesse-Gear / NTransform-Gear

按 `docs/` 已批准设计搭建的 C++20 实验框架：原始文件 → FastCDC → SHA-1 精确去重与唯一内存池 → 相同所选 chunk 集合 → 两种 SF 算法 → 计时、汇总和诊断。论文 Rabin 已依 D-05 替换为 Gear；原 SF 入口用于性能比较；新增 Delta 入口用于实际编码、恢复与 payload 质量验证。

当前包含核心算法、固定参数与 SF 编码、CDC、raw/manifest、SHA-1＋长度/逐字节确认的精确去重、内存唯一块存储、SHA-256 完整性校验、预热与五轮测量、诊断和 CTest。D-21 已批准内存池及默认 sfInput=unique，保留显式 all 对照。D-22～D-24 已批准并接入固定 xdelta 源码、Raw 基块/单层 Delta、正收益回退、两算法独立 EncodedStore 及销毁原文池后的全部恢复。磁盘容器/重载、400MB LRU、并行与稳定系统吞吐仍在后续范围。

准备阅读和审查代码时，先看 [代码 Review 导读](docs/13-code-review-guide.md)，按阅读路线核对算法、输入、计时和结果。本项目函数及头文件声明前使用中文说明用途、输入、输出和必要副作用；第三方 xdelta 保留上游原样，规范见 [代码风格](docs/05-coding-style.md)。

后续代理协作遵循 [AGENT.md](AGENT.md)，标准自动发现入口为 [AGENTS.md](AGENTS.md)：文档/论文冲突必须报告，涉及取舍须询问人类，实现改动同步文档。

## 源码目录

src 按职责分组：Algorithms（Finesse、NTransform、SuperFeature）、Hashing（GearHash、SHA-1）、Chunking（CDC）、Compression（DeltaCodec）、Storage（原文池/编码池）、Runtime（实验驱动/候选索引）。Common 和 main.cpp 保持位置，完整目录见 [架构说明](docs/03-architecture.md)。本次仅移动目录并同步 CMake/文档路径，源码内容及原 include 保持不变。

## 构建与测试

需要 CMake ≥ 4.2、C99/C++20 编译器、OpenSSL Crypto 开发库和 Python 3.9+（测试与样例生成）。算法与实验驱动不调用 Python。

```sh
cmake -S . -B build/debug -DCMAKE_BUILD_TYPE=Debug -DFINESSE_SANITIZERS=ON
cmake --build build/debug -j 4
ctest --test-dir build/debug --output-on-failure

cmake -S . -B build/release -DCMAKE_BUILD_TYPE=Release
cmake --build build/release -j 4
ctest --test-dir build/release --output-on-failure
```

在 CLion 中重新加载根目录 CMake 工程，选择 `Finesse` target，程序参数填写 `--config` 和配置文件绝对路径。原先根目录的 Hello World 入口已替换为 `src/main.cpp`。

## 运行批准的验收样例

```sh
python3 scripts/generate_synthetic.py tmp/synthetic-v2
build/release/Finesse --config configs/example.conf
```

样例共 3,670,118 字节（约 3.5 MiB），固定 xorshift32 种子 `0x13579bdf`，含常量、递增、重复、替换、插入删除、不同长度共享内容及 0～14 字节文件；实际长度、修改位置和生成规则记录在 `tmp/synthetic-v2/dataset.txt`。每次测量遍历 128 次。生成器和结果目录均拒绝覆盖；重复运行应选用新的目录与 `runId`。这些是验收数据，不代表论文的真实六数据集。重复内容现已按 D-21 入池去重，实际 SF 字节数由所选集合记录。

输出目录由配置指定，当前样例为 `results/synthetic-dedup-release-v1/`：

| 文件 | 内容 |
| --- | --- |
| `run.txt` | 状态、失败原因、平台、构建源码摘要、算法配置、输入摘要、计时边界和排除统计 |
| `source-files.csv` | 本次构建的源码、配置、文档与测试文件 SHA-256 快照 |
| `parameters.csv` | 实际 12 对变换参数；与版本化参数表校验 |
| `manifest.csv` | 全部原始出现的 `chunkId,file,offset,length`，路径相对清单目录 |
| `stored-chunks.csv` | 每个唯一 payload 的 storedId、首次 chunkId、长度、SHA-1、引用次数 |
| `chunk-references.csv` | 每次出现到 storedId 的映射，保留来源范围与是否重复 |
| `deduplication.csv` | 原始/唯一/重复计数及字节、storedPayloadBytes、两种去重比例、独立耗时 |
| `checksums.csv` | 清单和实际引用源文件的 SHA-256，供严格重放 |
| `source-checksums.csv` | 全部加载源文件（包括空文件）的路径、长度、SHA-256（D-18） |
| `measurements.csv` | 每算法五次原始测量；不包含预热 |
| `summary.csv` | 从原始计时得到的均值、总体标准差和速度比 |
| `diagnostics.csv` | 可选跨槽匹配；独立于 SF 计时 |

SF 计时包含从字节扫描到 SF 哈希及相同格式输出写入，不包含文件加载、CDC、SHA-1、精确查重、Store 插入、诊断索引或摘要计算。吞吐单位 MB/s = 10⁶ bytes/s；主要速度比为 NTransform 平均秒数 / Finesse 平均秒数。`inputBytes/chunkCount` 与原 excluded 字段描述所有原始出现；schema=2 新增 sfInputBytes/sfInputChunks 和 sfExcludedBytes/sfExcludedChunks 描述所选集合，`processedBytes=(sfInputBytes-sfExcludedBytes)*iterations`。summary 也记录 sfInput。

## 使用自己的文件

复制 `configs/example.conf`，按配置文件所在目录填写路径；raw 模式的 `inputPath` 指向 UTF-8 路径列表，一行一个文件，路径相对列表目录，不递归目录、不接受重复路径。设置 `datasetKind=real`，在 `datasetDescription` 记录来源。全部键必填且不得重复，未知键会报错；新增必填 sfInput=unique|all。unique 仅测首次出现的唯一块，all 遍历所有出现引用；两种模式实际存储均只保留一个相同 payload。算法语义与 CDC 参数仍为冻结值。

清单重放时设 `inputMode=manifest`，`inputPath` 指向已有 `manifest.csv`，保留同目录的 `checksums.csv`，并使用新的结果目录和 runId。清单按行顺序加载，源文件必须仍能按记录路径访问，摘要或范围错误会失败。

SF 入口使用每次实验独占的内存唯一原文池。raw 逐文件入池后释放原缓冲，manifest 的加载缓冲在返回前释放；测量和诊断通过 storedId 读取唯一 payload。uniqueBytes/storedPayloadBytes 只统计池内容，不含索引、对象或引用元数据。进程结束池释放，尚无磁盘持久化/重载与分批模式，CSV 不承载 payload。所有长度均参与精确存储；所选 SF 集合中小于 12 的块两算法统一排除，原始与所选短块分别统计；没有有效块时不输出性能汇总。输出目录已存在时拒绝覆盖；已创建目录中的失败运行保留 `status=failed` 和原因，不保留正常汇总。

## 当前数据：FastCDC 四组 Dataset（D-20）

使用同级 `../FastCDC/Dataset`，四组分别运行。所有配置和输入列表均使用相对路径；原数据只读，沿用 `.DS_Store`、`._*` 排除规则，分块参数仍为 D-16。

| 组 | 内容 | 文件数 | 字节数 | 配置 |
| --- | --- | --- | --- | --- |
| DataSet_1 | Emacs 压缩包 | 5 | 179761540 | [配置 1](configs/fastcdc-dataset-1.conf) |
| DataSet_2 | Emacs 源码（LNX 类） | 16982 | 636563018 | [配置 2](configs/fastcdc-dataset-2.conf) |
| DataSet_3 | 未压缩 tar（TAR 类） | 5 | 650055680 | [配置 3](configs/fastcdc-dataset-3.conf) |
| DataSet_4 | 合成备份（SYN 类） | 8 | 268435456 | [配置 4](configs/fastcdc-dataset-4.conf) |

第四组标为 `synthetic`，固定种子 20260917、规模与来源生成器均在配置中记录；当前文件已做内存重现核验。SF 入口报告 Gear 变体性能；Delta 入口单独报告一次质量/恢复验收。

```sh
python3 scripts/prepare_fastcdc_inputs.py
cmake --build build/release -j 4
build/release/Finesse --config configs/fastcdc-dataset-1.conf
build/release/Finesse --config configs/fastcdc-dataset-2.conf
build/release/Finesse --config configs/fastcdc-dataset-3.conf
build/release/Finesse --config configs/fastcdc-dataset-4.conf
```

路径列表在 `configs/inputs-fastcdc/`，相对列表目录读取原数据；配置再相对引用列表。脚本按项目目录解析路径，即使从其他工作目录启动也保持一致。现有清单无需重复生成；数据变更时应使用新清单目录和新配置。每组 sfInput=unique、每轮完整遍历唯一集合一次，预热一次、正式五次，精确去重和诊断独立于 SF 计时；四组顺序运行，每组新建独立 Store。结果为 `results/fastcdc-dataset-N-dedup-release-v1/`，存在则拒绝覆盖。详细来源和相对路径见 [当前数据说明](docs/12-fastcdc-dataset.md)，精确存储、所有权与计数见 [D-21](docs/14-sha1-chunk-store.md)。旧 `fastcdc-dataset-N-release-v1` 为全部块、未接入 Store 时的历史结果。

TTTD-S 两组配置与产物保留为 [D-19 历史方案](docs/11-tttd-dataset.md)。

设计、审批与来源见 [docs/README.md](docs/README.md)、[D-17/D-18](docs/09-configuration-proposal.md) 和 [源码摘取记录](docs/10-source-provenance.md)。

## 真实 Raw/Delta 内存存储（D-22～D-24）

```sh
build/release/Finesse --delta-config configs/fastcdc-dataset-1-delta.conf
build/release/Finesse --delta-config configs/fastcdc-dataset-2-delta.conf
build/release/Finesse --delta-config configs/fastcdc-dataset-3-delta.conf
build/release/Finesse --delta-config configs/fastcdc-dataset-4-delta.conf
python3 scripts/verify_delta_results.py results/fastcdc-dataset-1-delta-release-v1 results/fastcdc-dataset-2-delta-release-v1 results/fastcdc-dataset-3-delta-release-v1 results/fastcdc-dataset-4-delta-release-v1
```

`inputConfig` 相对引用原数据配置，要求 unique，仅导入输入/CDC/Gear，不执行原 SF 迭代，也不使用旧输出目录。新结果目录 `fastcdc-dataset-N-delta-release-v1` 同样拒绝覆盖。

顺序为 SHA-1 精确去重 → 唯一块 SF → 首个合格 Raw 基块 → `Delta=encode(B,A)` → 立即 `decode(B,Delta)==A` 逐字节核对 → 仅严格更小才保存 Delta。Delta 只保存 VCDIFF 与 baseId，没有 A 的原文副本，最大深度 1；重复出现引用同一编码记录。构建完两算法独立存储后，销毁原文池，再仅从 EncodedStore 恢复全部逻辑出现并校验 SHA-256。

新增 `encoded-chunks.csv`、`delta-attempts.csv`、`occurrences.csv`、`restoration-verification.csv` 与 `compression-summary.csv`。DCR 为全部唯一原文字节 / 最终 Raw＋Delta payload，包含短块、未匹配及回退 Raw；尝试/接受/实际存储的 residual/saving 分列。`deduplication.csv` 描述构造输入原文池，最终编码大小看 compression-summary。CSV 是元信息，payload 在进程内存，退出后释放；不能据此跨进程重载。

完整规格与配置见 [15](docs/15-xdelta-pipeline-proposal.md)，实测与验证见 [16](docs/16-delta-validation.md)。每组每算法构造/核验一次，不作为五轮系统吞吐结论；现有 SF 五轮入口保持独立。
