#include "EncodedStore.h"

#include "DeltaCodec/DeltaCodec.h"
#include "Experiment/IO.h"
#include "Finesse/Finesse.h"
#include "NTransform/NTransform.h"

#include <chrono>
#include <limits>
#include <stdexcept>

using SteadyClock = std::chrono::steady_clock;

static auto view(const std::string_view bytes) -> ChunkView {
    return {reinterpret_cast<const std::uint8_t *>(bytes.data()), bytes.size()};
}

static auto elapsedSince(const SteadyClock::time_point start) -> std::int64_t {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(SteadyClock::now() - start).count();
}

static auto checkedRecord(const EncodedStore &store, const StoredChunkId id) -> const EncodedChunk & {
    if (id >= store.chunks.size() || store.chunks[static_cast<std::size_t>(id)].id != id) {
        throw std::invalid_argument("invalid encoded chunk ID");
    }
    return store.chunks[static_cast<std::size_t>(id)];
}

static auto verifyBytes(const EncodedChunk &record, const std::string_view bytes) -> void {
    if (bytes.size() != record.originalLength || sha256(bytes) != record.restoredSha256) {
        throw std::runtime_error("encoded chunk length/integrity verification failed");
    }
}

auto restoreEncodedChunk(const EncodedStore &store, const StoredChunkId id) -> std::string {
    const auto &record = checkedRecord(store, id);
    if (record.kind == PayloadKind::Raw) {
        if (record.baseId) {
            throw std::invalid_argument("Raw chunk has base reference");
        }
        verifyBytes(record, record.payload);
        return record.payload;
    }
    if (record.kind != PayloadKind::Delta || !record.baseId || *record.baseId >= id
        || record.payload.size() >= record.originalLength) {
        throw std::invalid_argument("invalid Delta representation or base order");
    }
    const auto &base = checkedRecord(store, *record.baseId);
    if (base.kind != PayloadKind::Raw || base.baseId || base.originalLength < FEATURE_COUNT) {
        throw std::invalid_argument("Delta base must be eligible earlier Raw record; maximum depth is one");
    }
    verifyBytes(base, base.payload);
    if (record.originalLength > std::numeric_limits<std::size_t>::max()) {
        throw std::length_error("decoded chunk exceeds address space");
    }
    auto restored = decodeDelta(view(base.payload), record.payload, static_cast<std::size_t>(record.originalLength));
    verifyBytes(record, restored);
    return restored;
}

auto appendEncodedChunk(EncodedStore &store, const ChunkId firstChunkId, const ChunkView target,
                        const std::optional<Candidate> candidate) -> PayloadKind {
    if (target.length != 0 && target.data == nullptr) {
        throw std::invalid_argument("null target with nonzero length");
    }
    const std::string_view bytes(target.length == 0 ? "" : reinterpret_cast<const char *>(target.data), target.length);
    EncodedChunk record {store.chunks.size(), firstChunkId, PayloadKind::Raw, std::nullopt,
                         target.length, sha256(bytes), {}};
    std::optional<DeltaAttempt> attempt;
    if (candidate) {
        if (target.length < FEATURE_COUNT) {
            throw std::invalid_argument("short chunk cannot have SF candidate");
        }
        const auto &base = checkedRecord(store, candidate->chunkId);
        if (base.kind != PayloadKind::Raw || base.baseId || base.originalLength < FEATURE_COUNT
            || candidate->querySlot >= SUPER_FEATURE_COUNT || candidate->historicalSlot >= SUPER_FEATURE_COUNT) {
            throw std::invalid_argument("candidate must be eligible earlier Raw with valid SF slots");
        }
        verifyBytes(base, base.payload);
        const auto encodeStart = SteadyClock::now();
        auto delta = encodeDelta(view(base.payload), target);
        const auto encodeNs = elapsedSince(encodeStart);
        const auto verifyStart = SteadyClock::now();
        if (decodeDelta(view(base.payload), delta, bytes.size()) != bytes) {
            throw std::runtime_error("Xdelta roundtrip byte equality failed");
        }
        const auto verifyNs = elapsedSince(verifyStart);
        const bool accepted = delta.size() < bytes.size();
        attempt = DeltaAttempt {record.id, *candidate, bytes.size(), delta.size(), accepted, encodeNs, verifyNs};
        if (accepted) {
            record.kind = PayloadKind::Delta;
            record.baseId = base.id;
            record.payload = std::move(delta);
        }
    }
    if (record.kind == PayloadKind::Raw) {
        record.payload = bytes;
    }
    const auto kind = record.kind;
    // 追加是原子的：尝试记录分配失败时回滚内容记录。
    store.chunks.push_back(std::move(record));
    try {
        if (attempt) { store.attempts.push_back(*attempt); }
    } catch (...) {
        store.chunks.pop_back();
        throw;
    }
    return kind;
}

auto buildEncodedStore(const ExperimentInput &input, const std::string_view algorithm) -> EncodedStore {
    const auto start = SteadyClock::now();
    auto index = makeResemblanceIndex(algorithm, PROFILE_ID);
    EncodedStore output;
    output.algorithm = algorithm;
    output.chunks.reserve(input.store.chunks.size());
    const HashConfig hash;
    const auto parameters = approvedTransformParameters();
    for (const auto &source : input.store.chunks) {
        if (source.id != output.chunks.size()) {
            throw std::invalid_argument("input store IDs are not contiguous");
        }
        const auto firstPosition = input.uniqueChunkPositions.at(static_cast<std::size_t>(source.id));
        const auto &first = input.chunks.at(firstPosition);
        if (first.storedId != source.id || first.isDuplicate) {
            throw std::invalid_argument("input unique-position mapping is invalid");
        }
        SignatureResult signature;
        std::optional<Candidate> candidate;
        if (source.bytes.size() >= FEATURE_COUNT) {
            const auto target = view(source.bytes);
            signature = algorithm == "Finesse-Gear" ? extractFinesse(target, hash)
                : extractNTransform(target, hash, parameters);
            candidate = queryCandidate(index, source.id, signature, algorithm, PROFILE_ID);
        }
        const auto kind = appendEncodedChunk(output, first.id, view(source.bytes), candidate);
        if (kind == PayloadKind::Raw && signature.status == ExtractStatus::Ok) {
            insertSignature(index, source.id, signature, algorithm, PROFILE_ID);
        }
    }
    output.buildNs = elapsedSince(start);
    return output;
}
