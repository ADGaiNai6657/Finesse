# D-17：配置与首期运行入口（已批准）

用户已明确回复“批准 D-17 全部方案”。这是 `06-experiments.md` 要求在实现前补全并批准的具体配置方案。
算法语义沿用 D-01～D-12、D-14～D-16，不改动已批准事项。

后续 D-21 已批准启用精确去重，新增下面的必填 sfInput，并更新原来不启用 SHA-1 的部分；本页配置表据此列出当前入口。

## 入口与配置

命令：`Finesse --config <file>`。所有键必填、仅出现一次；未知键、空值和不合法值报错。
UTF-8 文本，每行 `key=value`，空行和以 `#` 开头的整行注释忽略；不支持行尾注释。
路径相对配置文件目录解析。结果目录已存在时失败，不覆盖。

| 键 | 值与含义 |
| --- | --- |
| `profileId` | 固定 `finesse-gear-v1`，绑定全部已批准算法语义 |
| `inputMode` | `raw`（排序文件清单、逐文件 CDC）或 `manifest`（校验已有 chunk 清单） |
| `inputPath` | raw 模式为 UTF-8 文件路径列表（一行一路径，相对列表目录）；manifest 模式为 CSV 路径 |
| `datasetKind` | `real` 或 `synthetic`，不得省略 |
| `datasetDescription` | 非空来源描述；synthetic 必须记录生成器、规模与种子 |
| `outputDirectory` | 本次运行独占的结果目录 |
| `runId` | 只含 ASCII 字母、数字、下划线、连字符的非空 ID |
| `expectedSize` | 固定 `8192` |
| `minSize` | 固定 `2048`；首期不支持 0，不改变上游切点语义 |
| `maxSize` | 固定 `65536` |
| `ncLevel` | 固定 `0` |
| `warmupRuns` | 固定 `1`，每算法各一次 |
| `measurementRuns` | 固定 `5`，每轮交替先后顺序 |
| `iterations` | 正整数，每次测量对相同 chunk 序列完整遍历的次数，按实际次数累计字节 |
| `diagnostics` | `true` 或 `false`；独立于 SF 计时的跨槽匹配 |
| `sfInput` | D-21：`unique` 或 `all`，当前配置明确写 unique；两种模式都启用唯一存储，决定 SF 遍历哪个集合 |

不再用独立键重复设定 Gear 表、编码、模数等冻结语义；profileId 对应完整 HashConfig，全部字段展开记录在 run.txt。
12 对参数作为版本化 CSV 随源码保存，严格校验，并在每次结果目录导出实用值与摘要。

## 输入完整性与输出

raw 模式只接受显式文件，不递归遍历目录；重复路径报错，重复内容保留。
manifest 表头固定为 `chunkId,file,offset,length`；文件路径相对清单目录。
重放 manifest 时，要求旁边存在 `checksums.csv`（`file,sha256`），记录清单和每个源文件的 SHA-256；缺失、额外、重复记录或校验失败均终止。
SHA-256 通过 OpenSSL Crypto 实现完整性校验；D-21 另用 SHA-1＋逐字节确认进行精确去重，两种用途分离。
每次运行导出 manifest.csv、checksums.csv、run.txt、parameters.csv、measurements.csv、summary.csv；开启诊断另导出 diagnostics.csv。
按 D-21 另导出 stored-chunks.csv、chunk-references.csv、deduplication.csv；内存 pool 承载唯一 payload，这三份 CSV 保存元信息、引用及统计。measurement schema=2 的追加字段见 14。
summary 使用五次测量的算术均值、总体标准差，以及 meanBaselineSeconds/meanFinesseSeconds。
没有有效 chunk 时失败并记录原因，不输出零分母性能结论。

## 验收用 synthetic 样例（已批准）

用项目内 Python 标准库脚本生成约 4 MiB 的固定样例：全零、全 0xff、递增字节、xorshift32 伪随机（初始种子 `0x13579bdf`）、局部替换、头/中/尾插入删除、共享前缀的不同长度配对，并加入 0～14 字节短文件。
具体文件长度、生成器、种子、修改位置写入样例描述文件；这些仅用于正确性与 Release 冒烟微基准，不代表真实数据集或论文实验结果。

审批记录：用户明确回复“批准 D-17 全部方案”，配置、输入校验与 synthetic 验收样例全部获批。

## D-18 补充（已批准）

用户明确回复“批准 D-18”：`checksums.csv` 仅含清单与实际引用源文件；`source-checksums.csv` 另存全部源文件（含零 chunk 的空文件）的路径、长度与摘要，`run.txt` 保存该文件摘要。此补充解决空文件在严格重放摘要集合中的归属，不改变空文件分块行为。

## D-24 独立 Delta 入口

本页配置继续用于 `--config` 的 SF 微基准；新增 `--delta-config` 使用独立的 16 键配置，相对 inputConfig 引用本页数据配置，要求 sfInput=unique，仅导入输入/CDC/Gear 和数据说明。不会执行原 SF 的 iterations/预热/测量/诊断，也不会使用原输出目录。新入口的全部键与输出契约见 [15](15-xdelta-pipeline-proposal.md)，示例为 configs/fastcdc-dataset-N-delta.conf。D-18 的空文件/源摘要契约继续沿用。
