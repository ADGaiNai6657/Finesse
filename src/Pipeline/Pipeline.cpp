#include "Pipeline.h"

#include <stdexcept>

auto makeResemblanceIndex(const std::string_view algorithm, const std::string_view profileId) -> ResemblanceIndex {
    if ((algorithm != "Finesse-Gear" && algorithm != "NTransform-Gear") || profileId != PROFILE_ID) {
        throw std::invalid_argument("unsupported index algorithm/profile");
    }
    return {std::string(algorithm), std::string(profileId), {}, {}};
}

static auto validateIndex(const ResemblanceIndex &index, const ChunkId chunkId, const SignatureResult &signature,
                          const std::string_view algorithm, const std::string_view profileId) -> void {
    if (signature.status != ExtractStatus::Ok || index.algorithm != algorithm || index.profileId != profileId) {
        throw std::invalid_argument("invalid signature or mixed index configuration");
    }
    if (index.insertedIds.contains(chunkId)) {
        throw std::invalid_argument("chunk ID already inserted");
    }
}

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

auto insertSignature(ResemblanceIndex &index, const ChunkId chunkId, const SignatureResult &signature,
                     const std::string_view algorithm, const std::string_view profileId) -> void {
    validateIndex(index, chunkId, signature, algorithm, profileId);
    index.insertedIds.insert(chunkId);
    for (std::size_t s = 0; s < SUPER_FEATURE_COUNT; ++s) {
        index.entries.emplace(signature.superFeatures[s], HistoricalSignature {chunkId, s});
    }
}

auto queryThenInsert(ResemblanceIndex &index, const ChunkId chunkId, const SignatureResult &signature,
                     const std::string_view algorithm, const std::string_view profileId) -> std::optional<Candidate> {
    const auto candidate = queryCandidate(index, chunkId, signature, algorithm, profileId);
    insertSignature(index, chunkId, signature, algorithm, profileId);
    return candidate;
}
