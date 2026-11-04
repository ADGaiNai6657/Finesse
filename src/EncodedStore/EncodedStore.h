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
auto appendEncodedChunk(EncodedStore &store, ChunkId firstChunkId, ChunkView target,
                        std::optional<Candidate> candidate) -> PayloadKind;
auto buildEncodedStore(const ExperimentInput &input, std::string_view algorithm) -> EncodedStore;
// 唯一恢复入口：只接收编码池与 ID，不接收原文池、源文件或原文缓存。
// 非法/自引用/未来/Delta 基块、损坏 payload、长度或 SHA-256 不符均抛异常。
auto restoreEncodedChunk(const EncodedStore &store, StoredChunkId id) -> std::string;
