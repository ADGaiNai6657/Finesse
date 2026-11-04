#pragma once

#include "Common/Types.h"

#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>

struct Candidate {
    ChunkId chunkId = 0;
    std::size_t querySlot = 0;
    std::size_t historicalSlot = 0;
};

struct HistoricalSignature {
    ChunkId chunkId = 0;
    std::size_t slot = 0;
};

// 每算法/配置各建一份。先查询再插入；不允许重复 ID，插入顺序即历史顺序。
struct ResemblanceIndex {
    std::string algorithm;
    std::string profileId;
    std::unordered_map<FingerprintValue, HistoricalSignature> entries;
    std::unordered_set<ChunkId> insertedIds;
};

auto makeResemblanceIndex(std::string_view algorithm, std::string_view profileId) -> ResemblanceIndex;
// 查询不改变索引；Delta 管线在最终表示为 Raw 时才显式插入。
auto queryCandidate(const ResemblanceIndex &index, ChunkId chunkId, const SignatureResult &signature,
                    std::string_view algorithm, std::string_view profileId) -> std::optional<Candidate>;
auto insertSignature(ResemblanceIndex &index, ChunkId chunkId, const SignatureResult &signature,
                     std::string_view algorithm, std::string_view profileId) -> void;
auto queryThenInsert(ResemblanceIndex &index, ChunkId chunkId, const SignatureResult &signature,
                     std::string_view algorithm, std::string_view profileId) -> std::optional<Candidate>;

// 历史接口边界；实际 Raw/Delta 内容见 EncodedStore，磁盘容器仍待 D-13。
enum class EncodingKind { Raw, Duplicate, Delta };
struct ChunkRecord {
    ChunkId chunkId = 0;
    std::string sourceFile;
    std::uint64_t sourceOffset = 0;
    std::uint64_t originalLength = 0;
    EncodingKind encoding = EncodingKind::Raw;
    std::optional<ChunkId> baseChunkId;
    std::uint64_t encodedLength = 0;
};
