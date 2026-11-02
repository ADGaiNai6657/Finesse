# Finesse-Gear / NTransform-Gear

按 `docs/` 已批准设计搭建的 C++20 实验框架：原始文件 → FastCDC → 相同 chunk 集合 → 两种 SF 算法 → 原始计时、汇总和可选跨槽诊断。论文 Rabin 已依 D-05 替换为 Gear；当前结果只说明 Gear 变体的 SF 计算性能。

首期包含核心算法、固定 12 对参数、完整 SF 编码、CDC 适配、raw/manifest 输入、SHA-256 完整性校验、单次预热与五次轮换测量、诊断索引、CTest。完整 Delta 压缩系统仍按 D-13 延后；没有编码器、恢复存储、DCR/DCE 或系统吞吐量结果。

准备阅读和审查代码时，先看 [代码 Review 导读](docs/13-code-review-guide.md)，按阅读路线核对算法、输入、计时和结果。

## 构建与测试

需要 CMake ≥ 4.2、C++20 编译器、OpenSSL Crypto 开发库和 Python 3.9+（测试与样例生成）。算法与实验驱动不调用 Python。

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

样例共 3,670,118 字节（约 3.5 MiB），固定 xorshift32 种子 `0x13579bdf`，含常量、递增、重复、替换、插入删除、不同长度共享内容及 0～14 字节文件；实际长度、修改位置和生成规则记录在 `tmp/synthetic-v2/dataset.txt`。每次测量遍历 128 次。生成器和结果目录均拒绝覆盖；重复运行应选用新的目录与 `runId`。这些是验收数据，不代表论文的真实六数据集。

输出目录由配置指定，样例为 `results/synthetic-release-v2/`：

| 文件 | 内容 |
| --- | --- |
| `run.txt` | 状态、失败原因、平台、构建源码摘要、算法配置、输入摘要、计时边界和排除统计 |
| `source-files.csv` | 本次构建的源码、配置、文档与测试文件 SHA-256 快照 |
| `parameters.csv` | 实际 12 对变换参数；与版本化参数表校验 |
| `manifest.csv` | 实际 `chunkId,file,offset,length` 序列，路径相对此清单目录 |
| `checksums.csv` | 清单和实际引用源文件的 SHA-256，供严格重放 |
| `source-checksums.csv` | 全部加载源文件（包括空文件）的路径、长度、SHA-256（D-18） |
| `measurements.csv` | 每算法五次原始测量；不包含预热 |
| `summary.csv` | 从原始计时得到的均值、总体标准差和速度比 |
| `diagnostics.csv` | 可选跨槽匹配；独立于 SF 计时 |

SF 计时包含从字节扫描到 SF 哈希及相同格式输出写入，不包含文件加载、CDC、诊断索引或摘要计算。吞吐单位 MB/s = 10⁶ bytes/s；主要速度比为 NTransform 平均秒数 / Finesse 平均秒数。`processedBytes` 包括 `iterations` 次完整遍历，`inputBytes` 与排除计数描述一次输入集合。

## 使用自己的文件

复制 `configs/example.conf`，按配置文件所在目录填写路径；raw 模式的 `inputPath` 指向 UTF-8 路径列表，一行一个文件，路径相对列表目录，不递归目录、不接受重复路径。设置 `datasetKind=real`，在 `datasetDescription` 记录来源。全部键必填且不得重复，未知键会报错；算法语义与 CDC 参数冻结为批准值。

清单重放时设 `inputMode=manifest`，`inputPath` 指向已有 `manifest.csv`，保留同目录的 `checksums.csv`，并使用新的结果目录和 runId。清单按行顺序加载，源文件必须仍能按记录路径访问，摘要或范围错误会失败。

当前模式将输入和输出缓冲全部放入内存；没有隐式分批模式。内存不足会失败，用户应选择可驻留内存的数据量。长度小于 12 的块两算法统一排除并统计；没有有效块时不输出性能汇总。输出目录已存在时拒绝覆盖；已创建目录中的失败运行保留 `status=failed` 和原因，不保留正常汇总。

## 当前数据：FastCDC 四组 Dataset（D-20）

使用同级 `../FastCDC/Dataset`，四组分别运行。所有配置和输入列表均使用相对路径；原数据只读，沿用 `.DS_Store`、`._*` 排除规则，分块参数仍为 D-16。

| 组 | 内容 | 文件数 | 字节数 | 配置 |
| --- | --- | --- | --- | --- |
| DataSet_1 | Emacs 压缩包 | 5 | 179761540 | [配置 1](configs/fastcdc-dataset-1.conf) |
| DataSet_2 | Emacs 源码（LNX 类） | 16982 | 636563018 | [配置 2](configs/fastcdc-dataset-2.conf) |
| DataSet_3 | 未压缩 tar（TAR 类） | 5 | 650055680 | [配置 3](configs/fastcdc-dataset-3.conf) |
| DataSet_4 | 合成备份（SYN 类） | 8 | 268435456 | [配置 4](configs/fastcdc-dataset-4.conf) |

第四组标为 `synthetic`，固定种子 20260917、规模与来源生成器均在配置中记录；当前文件已做内存重现核验。各组只报告本项目 Gear 变体的 SF 性能。

```sh
python3 scripts/prepare_fastcdc_inputs.py
cmake --build build/release -j 4
build/release/Finesse --config configs/fastcdc-dataset-1.conf
build/release/Finesse --config configs/fastcdc-dataset-2.conf
build/release/Finesse --config configs/fastcdc-dataset-3.conf
build/release/Finesse --config configs/fastcdc-dataset-4.conf
```

路径列表在 `configs/inputs-fastcdc/`，相对列表目录读取原数据；配置再相对引用列表。脚本按项目目录解析路径，即使从其他工作目录启动也保持一致。现有清单无需重复生成；数据变更时应使用新清单目录和新配置。每组每轮完整遍历一次，预热一次、正式五次，诊断独立于计时；四组顺序运行。结果为 `results/fastcdc-dataset-N-release-v1/`，存在则拒绝覆盖。详细来源与相对路径约定见 [当前数据说明](docs/12-fastcdc-dataset.md)。

TTTD-S 两组配置与产物保留为 [D-19 历史方案](docs/11-tttd-dataset.md)。

设计、审批与来源见 [docs/README.md](docs/README.md)、[D-17/D-18](docs/09-configuration-proposal.md) 和 [源码摘取记录](docs/10-source-provenance.md)。
