#include "EncodedStore.h"

#include "DeltaCodec/DeltaCodec.h"
#include "Experiment/IO.h"
#include "Finesse/Finesse.h"
#include "NTransform/NTransform.h"

#include <chrono>
#include <limits>
#include <stdexcept>

using SteadyClock = std::chrono::steady_clock;

//将字符串字节转换成只读 chunk 视图
//输入：bytes：调用期间有效的字节串。
//输出：返回借用该串内存的 ChunkView，不复制 payload。
static auto view(const std::string_view bytes) -> ChunkView {
    return {reinterpret_cast<const std::uint8_t *>(bytes.data()), bytes.size()};
}

//计算从指定时刻到当前的单调时钟耗时
//输入：start：steady_clock 起始时刻。
//输出：返回经过的纳秒数，不修改起始时刻。
static auto elapsedSince(const SteadyClock::time_point start) -> std::int64_t {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(SteadyClock::now() - start).count();
}

//按 ID 检查并取得编码池记录
//输入：store：编码池；id：目标 encoded/stored ID。
//输出：返回只读记录引用；越界或记录自身 ID 不一致时抛异常，引用依赖池生命周期。
static auto checkedRecord(const EncodedStore &store, const StoredChunkId id) -> const EncodedChunk & {
    if (id >= store.chunks.size() || store.chunks[static_cast<std::size_t>(id)].id != id) {
        throw std::invalid_argument("invalid encoded chunk ID");
    }
    return store.chunks[static_cast<std::size_t>(id)];
}

//检查恢复内容的长度和 SHA-256 是否与编码记录一致
//输入：record：记录的原长和完整性摘要；bytes：待验证内容。
//输出：无返回值；长度或摘要不符时抛异常，不修改记录和字节。
static auto verifyBytes(const EncodedChunk &record, const std::string_view bytes) -> void {
    const metrics::Timer verification("chunk_sha256_verify", bytes.size());
    if (bytes.size() != record.originalLength || sha256(bytes) != record.restoredSha256) {
        throw std::runtime_error("encoded chunk length/integrity verification failed");
    }
}

//仅从编码池恢复一个唯一 chunk
//输入：store：拥有 Raw/Delta payload 的编码池；id：要恢复的唯一 ID，不接收原文池或源文件。
//输出：返回拥有原文字节的 string；只允许此前合格 Raw 基块、深度 1，并校验长度和 SHA-256，损坏或非法引用抛异常。
auto restoreEncodedChunk(const EncodedStore &store, const StoredChunkId id) -> std::string {
    const auto &record = checkedRecord(store, id);
    if (record.kind == PayloadKind::Raw) {
        if (record.baseId) {
            throw std::invalid_argument("Raw chunk has base reference");
        }
        verifyBytes(record, record.payload);
        const metrics::Timer read("raw_read", record.payload.size());
        metrics::count("raw_read");
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
    metrics::Timer decoding("delta_decode", record.payload.size(), true);
    auto restored = decodeDelta(view(base.payload), record.payload, static_cast<std::size_t>(record.originalLength));
    decoding.finish("ok", restored.size(), base.id);
    metrics::count("delta_decode");
    verifyBytes(record, restored);
    return restored;
}

//为一次唯一内容真实编码并选择 Raw 或 Delta 表示
//输入：store：待追加编码池；firstChunkId：首次出现 ID；target：原文视图；candidate：可选的此前合格 Raw 候选。
//输出：返回最终表示并追加一条记录；匹配时 encode 后完整字节回放，仅严格更小才存 Delta，重复应由上层过滤，错误抛异常。
auto appendEncodedChunk(EncodedStore &store, const ChunkId firstChunkId, const ChunkView target,
                        const std::optional<Candidate> &candidate) -> PayloadKind {
    if (target.length != 0 && target.data == nullptr) {
        throw std::invalid_argument("null target with nonzero length");
    }
    const std::string_view bytes(target.length == 0 ? "" : reinterpret_cast<const char *>(target.data), target.length);
    metrics::Timer digest("target_sha256", bytes.size());
    EncodedChunk record {store.chunks.size(), firstChunkId, PayloadKind::Raw, std::nullopt,
                         target.length, sha256(bytes), {}};
    digest.finish();
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
        std::int64_t encodeNs = 0, verifyNs = 0;
        // Xdelta 编码与即时回放独立计时，基块摘要检查在两者之外。
        metrics::Timer encoding("delta_encode", target.length, true, &encodeNs);
        auto delta = encodeDelta(view(base.payload), target);
        encoding.finish(delta.size() < bytes.size() ? "accepted" : "raw_fallback", delta.size(), base.id);
        metrics::Timer verify("roundtrip_verify", target.length, true, &verifyNs);
        if (decodeDelta(view(base.payload), delta, bytes.size()) != bytes) {
            throw std::runtime_error("Xdelta roundtrip byte equality failed");
        }
        verify.finish("ok", bytes.size(), base.id);
        const bool accepted = delta.size() < bytes.size();  //只在编码后所消耗的存储空间小于原本的存储空间时接受
        attempt = DeltaAttempt {record.id, *candidate, bytes.size(), delta.size(), accepted, encodeNs, verifyNs};
        if (accepted) {
            record.kind = PayloadKind::Delta;
            record.baseId = base.id;
            record.payload = std::move(delta);
        }
    }
    metrics::Timer storage("encoded_payload_write", target.length);
    if (record.kind == PayloadKind::Raw) {
        record.payload = bytes;
    }
    const auto kind = record.kind;
    const auto storedBytes = record.payload.size();
    // 追加是原子的：尝试记录分配失败时回滚内容记录。
    store.chunks.push_back(std::move(record));
    try {
        if (attempt) { store.attempts.push_back(*attempt); }
    } catch (...) {
        store.chunks.pop_back();
        throw;
    }
    storage.finish("ok", storedBytes);
    return kind;
}

//按首次出现顺序为指定算法构造独立编码池
//输入：input：已精确去重的原文池和首次出现映射；algorithm：批准的算法名。
//输出：返回拥有 Raw/Delta 内容的 EncodedStore，不保留原文视图；只有最终合格 Raw 入候选索引，错误抛异常。
auto buildEncodedStore(const ExperimentInput &input, const std::string_view algorithm) -> EncodedStore {
    const metrics::Scope scope(algorithm, "build");
    const auto start = SteadyClock::now();
    const metrics::Timer build("encoded_pool_build");
    auto index = makeResemblanceIndex(algorithm, PROFILE_ID);
    EncodedStore output;
    output.algorithm = algorithm;
    output.chunks.reserve(input.store.chunks.size());
    constexpr HashConfig hash;
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
        const metrics::Scope chunk(first.fileIndex, first.id);
        SignatureResult signature;
        std::optional<Candidate> candidate;
        if (source.bytes.size() >= FEATURE_COUNT) {
            const auto target = view(source.bytes);
            // 特征提取结束之后才进入 FirstFit 索引查询。
            metrics::Timer extraction("feature_extraction", target.length);
            signature = algorithm == "Finesse-Gear" ? extractFinesse(target, hash)
                : extractNTransform(target, hash, parameters);
            extraction.finish();
            candidate = queryCandidate(index, source.id, signature, algorithm, PROFILE_ID);
        }
        const auto kind = appendEncodedChunk(output, first.id, view(source.bytes), candidate);
        if (kind == PayloadKind::Raw && signature.status == ExtractStatus::Ok) {
            insertSignature(index, source.id, signature, algorithm, PROFILE_ID);
        }
    }
    output.buildNs = elapsedSince(start);
    if (metrics::active()) metrics::count("sf_index_entries", index.entries.size());
    return output;
}
