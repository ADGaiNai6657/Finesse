#pragma once

#include "Experiment/Experiment.h"
#include "Pipeline/Pipeline.h"

#include <optional>
#include <string>
#include <vector>

// encoded ID 与输入 storedId 一一对应；首次出现的 chunkId 另列，不能混用。
enum class PayloadKind { Raw, Delta };
struct EncodedChunk {
    StoredChunkId id = 0;
    ChunkId firstChunkId = 0;
    PayloadKind kind = PayloadKind::Raw;
    std::optional<StoredChunkId> baseId;
    std::uint64_t originalLength = 0;
    std::string restoredSha256;
    std::string payload; // Raw 或 VCDIFF；Delta 绝不另存原文。
};
struct DeltaAttempt {
    StoredChunkId id = 0;
    Candidate candidate;
    std::uint64_t originalBytes = 0;
    std::uint64_t deltaBytes = 0;
    bool accepted = false;
    std::int64_t encodeNs = 0;
    std::int64_t verifyNs = 0;
};
struct EncodedStore {
    std::string algorithm;
    std::vector<EncodedChunk> chunks;
    std::vector<DeltaAttempt> attempts;
    std::int64_t buildNs = 0;
};

// 低层追加一次唯一内容；候选必须指向此前 Raw。返回实际选择的表示。
// 精确去重/候选索引由 buildEncodedStore 的上层处理，不能对重复出现重复调用。
//为一次唯一内容真实编码并选择 Raw 或 Delta 表示
//输入：store：待追加编码池；firstChunkId：首次出现 ID；target：原文视图；candidate：可选的此前合格 Raw 候选。
//输出：返回最终表示并追加一条记录；匹配时 encode 后完整字节回放，仅严格更小才存 Delta，重复应由上层过滤，错误抛异常。
auto appendEncodedChunk(EncodedStore &store, ChunkId firstChunkId, ChunkView target,
                        const std::optional<Candidate> &candidate) -> PayloadKind;
//按首次出现顺序为指定算法构造独立编码池
//输入：input：已精确去重的原文池和首次出现映射；algorithm：批准的算法名。
//输出：返回拥有 Raw/Delta 内容的 EncodedStore，不保留原文视图；只有最终合格 Raw 入候选索引，错误抛异常。
auto buildEncodedStore(const ExperimentInput &input, std::string_view algorithm) -> EncodedStore;
// 唯一恢复入口：只接收编码池与 ID，不接收原文池、源文件或原文缓存。
// 非法/自引用/未来/Delta 基块、损坏 payload、长度或 SHA-256 不符均抛异常。
//仅从编码池恢复一个唯一 chunk
//输入：store：拥有 Raw/Delta payload 的编码池；id：要恢复的唯一 ID，不接收原文池或源文件。
//输出：返回拥有原文字节的 string；只允许此前合格 Raw 基块、深度 1，并校验长度和 SHA-256，损坏或非法引用抛异常。
auto restoreEncodedChunk(const EncodedStore &store, StoredChunkId id) -> std::string;
