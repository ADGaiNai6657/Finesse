#include "Pipeline.h"

#include <stdexcept>

//为一个算法和配置建立独立的空 SF 候选索引
//输入：algorithm：Finesse-Gear 或 NTransform-Gear；profileId：已批准配置标识。
//输出：返回空索引；不支持的算法或配置抛异常。
auto makeResemblanceIndex(const std::string_view algorithm, const std::string_view profileId) -> ResemblanceIndex {
    if ((algorithm != "Finesse-Gear" && algorithm != "NTransform-Gear") || profileId != PROFILE_ID) {
        throw std::invalid_argument("unsupported index algorithm/profile");
    }
    return {std::string(algorithm), std::string(profileId), {}, {}};
}

//检查签名和索引配置，并拒绝重复插入的 ID
//输入：index：候选索引；chunkId：当前 ID；signature：签名；algorithm/profileId：本次配置标识。
//输出：无返回值，不修改索引；签名无效、配置混用或 ID 已插入时抛异常。
static auto validateIndex(const ResemblanceIndex &index, const ChunkId chunkId, const SignatureResult &signature,
                          const std::string_view algorithm, const std::string_view profileId) -> void {
    if (signature.status != ExtractStatus::Ok || index.algorithm != algorithm || index.profileId != profileId) {
        throw std::invalid_argument("invalid signature or mixed index configuration");
    }
    if (index.insertedIds.contains(chunkId)) {
        throw std::invalid_argument("chunk ID already inserted");
    }
}

//按当前槽位升序跨槽查找第一个历史候选
//输入：index：本算法索引；chunkId：尚未插入的当前 ID；signature：有效签名；algorithm/profileId：匹配配置。
//输出：返回候选的 ID、当前槽和历史槽，未命中返回空；不修改索引，同键保留最早插入者。
auto queryCandidate(const ResemblanceIndex &index, const ChunkId chunkId, const SignatureResult &signature,
                    const std::string_view algorithm, const std::string_view profileId) -> std::optional<Candidate> {
    validateIndex(index, chunkId, signature, algorithm, profileId);
    for (std::size_t s = 0; s < SUPER_FEATURE_COUNT; ++s) {
        const auto found = index.entries.find(signature.superFeatures[s]);
        if (found != index.entries.end()) {
            return Candidate {found->second.chunkId, s, found->second.slot};
        }
    }
    return std::nullopt;
}

//将一个合格块的 SF 加入历史候选索引
//输入：index：待更新索引；chunkId：未插入 ID；signature：有效签名；algorithm/profileId：匹配配置。
//输出：无返回值；记录 ID 和 SF，同键保留最早者；Delta 管线由调用者保证只插入最终合格 Raw。
auto insertSignature(ResemblanceIndex &index, const ChunkId chunkId, const SignatureResult &signature,
                     const std::string_view algorithm, const std::string_view profileId) -> void {
    validateIndex(index, chunkId, signature, algorithm, profileId);
    index.insertedIds.insert(chunkId);
    for (std::size_t s = 0; s < SUPER_FEATURE_COUNT; ++s) {
        index.entries.emplace(signature.superFeatures[s], HistoricalSignature {chunkId, s});
    }
}

//为原 SF 诊断执行先查候选、再插入当前签名
//输入：index、chunkId、signature：索引和当前签名；algorithm/profileId：匹配配置。
//输出：返回插入前的可选历史候选，并更新索引；不实施 Delta 管线的 Raw 资格筛选。
auto queryThenInsert(ResemblanceIndex &index, const ChunkId chunkId, const SignatureResult &signature,
                     const std::string_view algorithm, const std::string_view profileId) -> std::optional<Candidate> {
    const auto candidate = queryCandidate(index, chunkId, signature, algorithm, profileId);
    insertSignature(index, chunkId, signature, algorithm, profileId);
    return candidate;
}
