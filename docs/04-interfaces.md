# 接口与数据契约

## 状态

本文件接口方案已批准 D-10，已在 src/ 中建立头文件；GearHash 替换已批准 D-05，具体状态及 SF 编码见已批准 D-14；D-04 短块规则已批准。

沿用 FastCDC 的普通函数、简单结构体、指针加长度和尾置返回类型写法。首期不增加虚基类层次或插件注册系统。

## 基础类型草案

```cpp
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

static constexpr std::size_t FEATURE_COUNT = 12;
static constexpr std::size_t SUPER_FEATURE_COUNT = 3;

// GearHash 来源使用 64 位状态；N-transform 模 2^32 的结果零扩展。
using FingerprintValue = std::uint64_t;
using FeatureValues = std::array<FingerprintValue, FEATURE_COUNT>;
using SuperFeatures = std::array<FingerprintValue, SUPER_FEATURE_COUNT>;

struct ChunkView {
    const std::uint8_t *data = nullptr;
    std::size_t length = 0;
};

enum class ExtractStatus {
    Ok,
    InvalidInput,
    InvalidConfig,
    UnsupportedLength
};

struct SignatureResult {
    ExtractStatus status = ExtractStatus::InvalidInput;
    FeatureValues features {};
    SuperFeatures superFeatures {};
};

struct TransformParameters {
    std::array<std::uint32_t, FEATURE_COUNT> multipliers {};
    std::array<std::uint32_t, FEATURE_COUNT> addends {};
};

struct HashConfig;  // 实现时按已批准 D-14 定义；不得保留运行时空配置。

auto extractFinesse(ChunkView chunk, const HashConfig &config)
    -> SignatureResult;

auto extractNTransform(ChunkView chunk, const HashConfig &config,
                       const TransformParameters &parameters)
    -> SignatureResult;

auto groupFinesseFeatures(const FeatureValues &features)
    -> std::array<std::array<FingerprintValue, 4>, 3>;

auto groupNTransformFeatures(const FeatureValues &features)
    -> std::array<std::array<FingerprintValue, 4>, 3>;
```

`N-transform` 继续使用模 `2^32` 的结果，容器高位为零；Finesse 使用 Gear 最大值。统一 8 字节小端 SF 编码已由 D-14 批准，属于冻结的接口协议。

## 所有权与状态

| 项目 | 契约 |
| --- | --- |
| 输入所有权 | `ChunkView` 不拥有内存，调用期间由调用方保持有效；被调函数不保存指针 |
| 输入修改 | 不修改输入，支持二进制零字节，不使用 `strlen` 推断长度 |
| 空指针 | `length>0 && data==nullptr` 返回 `InvalidInput` |
| 空输入和短输入 | 按已批准 D-04返回 `UnsupportedLength`；无有效签名 |
| 输出 | 按值返回；仅 `status==Ok` 时 features 和 SF 有效；失败时数组为零但不得被索引 |
| 全局状态 | 不允许可变全局哈希状态、隐式随机源或跨调用累积 feature |
| 可重入 | 同一输入和配置返回相同输出；每次调用拥有自己的滚动状态 |
| 分配 | 核心 12/3 配置用定长数组；哈希表等可预构建资源由配置持有，不在逐字节循环中分配 |
| 异常 | 可预期的输入/配置问题用状态返回；驱动层捕获 I/O、分配等异常并使整次实验失败，不跳过后报成功 |

Gear 状态采用来源中的 `std::uint64_t` 值或引用，`gearRoll` 返回新状态，`updateGearHash` 回写引用。状态归属于当前 chunk，不与 CDC 或其他 chunk 共享。

## D-21 已接入的精确去重与存储接口

实际声明见 [Hash.h](../src/Hash/Hash.h)、[ChunkStore.h](../src/ChunkStore/ChunkStore.h)、[Experiment.h](../src/Experiment/Experiment.h)。

| 接口/类型 | 契约 |
| --- | --- |
| `sha1(string_view)` | 完整 20 字节 SHA-1；EVP 错误抛出异常，不以失败摘要继续查重 |
| `makeChunkStore(sha1)` | 本次实验独占的空池；真实运行固定使用 SHA-1，测试可注入碰撞函数 |
| `findDuplicate(store, ChunkView)` | 查 SHA-1 桶后按长度和字节确认，返回 optional StoredChunkId；不插入、不计数 |
| `storeChunk(store, ChunkView)` | 已有内容返回同一 storedId；新内容才复制 payload；计数或分配失败时不提交半次插入 |
| `readStoredChunk / storedChunkView` | 只读借用自持有的唯一 payload，普通追加不使旧视图失效 |
| `resetStore` | 清空池与统计，所有旧视图失效；禁止在仍使用视图的测量中调用 |
| `SourceFile` | 只保留 path、length、SHA-256，无原文件字节所有权 |
| `InputChunk` | 每次出现的 chunkId、来源、范围及 storedId/isDuplicate 引用 |
| `selectSFInput` | unique 取首次出现，all 取所有引用；明确统计所选集合，两个算法共享 |

池内 storedId 按首次出现顺序递增，与原始 chunkId 分离。唯一块内容入池后不可修改，Store 不使用可变全局状态。raw 模式按文件释放源缓冲，manifest 的加载缓冲在返回前全部销毁。SHA-1 碰撞保留不同内容的多个唯一块。完整格式、所有权和计数定义见 [D-21](14-sha1-chunk-store.md)。

## 哈希配置逻辑字段

下表是必须表达的信息，不是已确定的具体值或 C++ 存储布局。

| 字段 | 用途 |
| --- | --- |
| profileId / version | 区分不同批准配置，避免签名混用 |
| hashKind / stateBits | Gear / 64；不设置 Rabin 窗口 |
| gearTableRevision / initialValue | 固定表来源与初值 |
| updatePosition / subchunkReset | 更新后取值及跨子块状态规则，见 D-14 |
| featureEncoding / superFeatureHashProfile | 明确 SF 输入编码和哈希 |
| subchunkPolicy / shortChunkPolicy | 绑定 D-04，不能使用隐式默认 |

加载后先验证配置完整性，再构建热路径需要的查找表；禁止在每个字节处解析字符串配置。

## 系统接口与磁盘预留

`[已批准 D-10、D-09；完整系统行为细化待 D-13]` 下表保留系统逻辑能力名称，并非全部对应同名实际函数；具体已实现接口见本页最后一节。内存精确查重、原文池读取和 SF 候选已有具体接口；Delta 编解码与 EncodedStore 按 D-22～D-24 已实现，磁盘存储继续预留。

| 接口能力 | 输入 | 输出 | 契约 |
| --- | --- | --- | --- |
| `readNextChunk` | 输入流状态 | 拥有字节的 chunk / EOF / Error | EOF 与错误分开；来源范围可追溯 |
| `findDuplicate` | Store 和原始字节 | 可选 StoredChunkId | D-21 已批准并实现 SHA-1＋长度/逐字节确认，与 SF 索引分离 |
| `findFirstMatch` | 算法标识、配置标识、SF | 可选 Candidate | 未匹配不是错误；不返回当前或未来 chunk |
| `insertSignature` | ChunkId、SF | 插入结果 | 可检索基块和签名绑定；错误不能静默忽略 |
| `readBaseChunk` | ChunkId | 拥有原始字节的缓冲区 | 不返回仍需解码却伪称原文的数据 |
| `encodeDelta` | base、target 的只读视图 | 拥有编码字节的结果 / Error | 记录原始长度与基块 ID |
| `decodeDelta` | base、delta | 拥有恢复字节的结果 / Error | 可验证输出长度和内容 |
| `writeChunkRecord` | Raw / Duplicate / Delta 标签记录 | 存储位置 / Error | 不以哨兵 ID 代替可选字段 |

建议 `ChunkId` 用显式无符号整数标识，磁盘偏移和累计字节用 64 位整数。实际容器格式、跨进程兼容性、索引与数据提交顺序均留待 D-13，不在首期默认为已解决。

## FastCDC 适配契约

[已批准 D-11 来源；具体运行配置见 D-16] 复用 `makeFastCDCConfig(expectedSize,minSize,ncLevel)` 与 `BoundariesFinder(data,positions,config)`。保留上游接口名称与参数结构；适配层将本次新增的一组切点转换为 `[previousCut,cut)` 的 ChunkView，初始 previousCut=0。空文件产生零块；非空文件末切点必须等于文件长度，其余切点严格递增。缓冲区必须覆盖所有 ChunkView 的使用周期。禁止把无进展切点静默跳过后报告成功。

## D-22～D-24 的实际内存接口

| 接口 | 所有权与契约 |
| --- | --- |
| `queryCandidate(index,id,signature,algorithm,profile)` | 不改变 index，跨槽 FirstFit，混用配置/非法签名/已插入 ID 拒绝 |
| `insertSignature(...)` | 同键保留首次插入；Delta 管线仅对最终 Raw 且长度≥12 调用 |
| `queryThenInsert(...)` | 保留旧诊断的查询后插入所有合格签名行为 |
| `encodeDelta(base,target)` | 借用输入，返回拥有 VCDIFF 的 string，固定 level6/Adler32/无二级压缩，非 ENOSPC 错误抛异常 |
| `decodeDelta(base,delta,expectedLength)` | 返回拥有恢复字节的 string，输出长度严格相符，验证上游 Adler32 |
| `appendEncodedChunk(store,firstChunkId,target,candidate)` | 为一次唯一内容分配连续 encoded ID，真实 encode+decode 全字节比较后选择表示；候选必须为此前合格 Raw；重复不在此层处理 |
| `buildEncodedStore(input,algorithm)` | 借用已去重输入，按首次出现顺序建立独立结果，返回不含原文借用的 owning EncodedStore |
| `restoreEncodedChunk(store,id)` | 只依赖编码池；Raw 校验后返回复制，Delta 检查 earlier Raw/base/depth/长度/摘要后解码；非法/损坏抛异常 |
| `loadDeltaExperimentConfig(path)` / `runDeltaExperiment(config)` | 新入口，16 键严格解析，引用 inputConfig 要求 unique；单次构造、原文池析构后全恢复 |

具体字段、ID 语义、配置和统计见 [15](15-xdelta-pipeline-proposal.md)；`ChunkRecord` 历史磁盘边界不承载真实 payload。EncodedChunk.kind 只有 Raw/Delta，精确重复属于 occurrence 引用，不能再创建 Duplicate payload。所有指针视图只在调用期间借用；恢复结果拥有字节，不返回对临时解码区的视图。
