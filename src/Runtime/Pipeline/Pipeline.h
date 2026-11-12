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

//为一个算法和配置建立独立的空 SF 候选索引
//输入：algorithm：Finesse-Gear 或 NTransform-Gear；profileId：已批准配置标识。
//输出：返回空索引；不支持的算法或配置抛异常。
auto makeResemblanceIndex(std::string_view algorithm, std::string_view profileId) -> ResemblanceIndex;
// 查询不改变索引；Delta 管线在最终表示为 Raw 时才显式插入。
//按当前槽位升序跨槽查找第一个历史候选
//输入：index：本算法索引；chunkId：尚未插入的当前 ID；signature：有效签名；algorithm/profileId：匹配配置。
//输出：返回候选的 ID、当前槽和历史槽，未命中返回空；不修改索引，同键保留最早插入者。
auto queryCandidate(const ResemblanceIndex &index, ChunkId chunkId, const SignatureResult &signature,
                    std::string_view algorithm, std::string_view profileId) -> std::optional<Candidate>;
//将一个合格块的 SF 加入历史候选索引
//输入：index：待更新索引；chunkId：未插入 ID；signature：有效签名；algorithm/profileId：匹配配置。
//输出：无返回值；记录 ID 和 SF，同键保留最早者；Delta 管线由调用者保证只插入最终合格 Raw。
auto insertSignature(ResemblanceIndex &index, ChunkId chunkId, const SignatureResult &signature,
                     std::string_view algorithm, std::string_view profileId) -> void;
//为原 SF 诊断执行先查候选、再插入当前签名
//输入：index、chunkId、signature：索引和当前签名；algorithm/profileId：匹配配置。
//输出：返回插入前的可选历史候选，并更新索引；不实施 Delta 管线的 Raw 资格筛选。
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
