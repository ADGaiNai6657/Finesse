#include "Pipeline.h"

#include <stdexcept>

auto makeResemblanceIndex(const std::string_view algorithm, const std::string_view profileId) -> ResemblanceIndex {
    if ((algorithm != "Finesse-Gear" && algorithm != "NTransform-Gear") || profileId != PROFILE_ID) {
        throw std::invalid_argument("unsupported index algorithm/profile");
    }
    return {std::string(algorithm), std::string(profileId), {}, {}};
}

auto queryThenInsert(ResemblanceIndex &index, const ChunkId chunkId, const SignatureResult &signature,
                     const std::string_view algorithm, const std::string_view profileId) -> std::optional<Candidate> {
    if (signature.status != ExtractStatus::Ok || index.algorithm != algorithm || index.profileId != profileId) {
        throw std::invalid_argument("invalid signature or mixed index configuration");
    }
    if (index.insertedIds.contains(chunkId)) {
        throw std::invalid_argument("chunk ID already inserted");
    }
    std::optional<Candidate> candidate;
    for (std::size_t s = 0; s < SUPER_FEATURE_COUNT; ++s) {
        const auto found = index.entries.find(signature.superFeatures[s]);
        if (found != index.entries.end()) {
            candidate = Candidate {found->second.chunkId, s, found->second.slot};
            break;
        }
    }
    index.insertedIds.insert(chunkId);
    for (std::size_t s = 0; s < SUPER_FEATURE_COUNT; ++s) {
        // emplace 保留最早块；同一块重复槽自然保留最小槽号。
        index.entries.emplace(signature.superFeatures[s], HistoricalSignature {chunkId, s});
    }
    return candidate;
}
