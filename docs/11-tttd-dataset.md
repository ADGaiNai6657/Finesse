# TTTD-S 数据集接入（D-19 已批准）

历史方案：当前输入已按用户后续指示 D-20 切换为 [FastCDC 四组 Dataset](12-fastcdc-dataset.md)。本页和既有产物保留用于追溯。

用户已指定暂用既有 TTTD-S Dataset，并以相对路径读取。数据目录是本项目同级仓库的 `../TTTD-S_Experiments/DataSet`，只读使用，不复制、解压或改写原始数据。它不等于 Finesse 论文原始六数据集。

## 已核实输入

| 组 | 来源目录（相对本项目根目录） | 旧程序枚举方式 | 文件数 | 原始字节数 |
| --- | --- | --- | --- | --- |
| DataSet_1 | `../TTTD-S_Experiments/DataSet/DataSet_1` | 仅当前层普通文件 | 5 | 179761540 |
| DataSet_2 | `../TTTD-S_Experiments/DataSet/DataSet_2` | 递归普通文件 | 16983 | 636569166 |

DataSet_1 是 5 个 `.tar.gz` 压缩包；DataSet_2 是 5 个版本的 Emacs 源码树，包含根目录 `.DS_Store`（6148 字节）。目前未发现符号链接。旧程序没有排除 `.DS_Store`；排除后 DataSet_2 为 16982 个文件、636563018 字节。

## 具体接入方案

- 两组分别建立配置和结果目录，以区分压缩包与展开源码的输入特征。
- 配置文件分别为 `configs/tttd-s-dataset-1.conf`、`configs/tttd-s-dataset-2.conf`；`inputPath` 分别为 `inputs/tttd-s-dataset-1.files.txt`、`inputs/tttd-s-dataset-2.files.txt`，相对配置文件目录。
- 路径列表每行使用相对列表所在目录的路径，例如 `../../../TTTD-S_Experiments/DataSet/DataSet_1/emacs-21.4a.tar.gz`。程序原有 raw 模式继续只接受显式文件清单；目录枚举在测量前通过脚本准备，不新增隐式递归输入模式。
- DataSet_1 仅枚举当前层文件，DataSet_2 递归枚举，均按路径排序；重复内容保留。
- `datasetKind=real`，注明来源及筛选策略；两组使用固定 Gear profile 和 D-16 FastCDC 参数。不会改用 TTTD-S 的分块算法。
- `iterations=1`（每轮完整扫描整组数据），每算法一次预热、五次正式测量，`diagnostics=true`。结果目录分别为 `results/tttd-s-dataset-1-release-v1`、`results/tttd-s-dataset-2-release-v1`，存在则拒绝覆盖。
- 运行时仍保存每个源文件、清单和算法参数的摘要；路径由配置和列表的位置解析，不依赖启动工作目录。

## 已批准的输入选择

用户明确选择“两组分别运行，排除 macOS 元数据（建议）”：排除 `.DS_Store` 和 `._*` 文件。当前只有一个 `.DS_Store` 会受影响，其余数据不变。实际纳入 DataSet_1 的 5 个文件、179761540 字节，以及 DataSet_2 的 16982 个文件、636563018 字节。

准备脚本 `scripts/prepare_tttd_inputs.py` 只读枚举两组，生成 `configs/inputs/` 下的路径列表、`inventory.csv` 与 `excluded-files.csv`。路径列表和配置不含机器专属绝对路径；运行结果继续记录实际解析后的输入路径及文件摘要以便追溯。原数据变更时应显式准备新清单；脚本拒绝覆盖现有目录。

```sh
python3 scripts/prepare_tttd_inputs.py
cmake --build build/release -j 4
build/release/Finesse --config configs/tttd-s-dataset-1.conf
build/release/Finesse --config configs/tttd-s-dataset-2.conf
```

也可以从构建目录执行 `./Finesse --config ../../configs/tttd-s-dataset-1.conf`；配置与列表的相对路径解析不受工作目录变化影响。
