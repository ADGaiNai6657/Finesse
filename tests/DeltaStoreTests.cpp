#include "DeltaCodec/DeltaCodec.h"
#include "EncodedStore/EncodedStore.h"
#include "Experiment/IO.h"
#include "Finesse/Finesse.h"
#include "NTransform/NTransform.h"

#include <algorithm>
#include <iostream>
#include <limits>
#include <stdexcept>

static auto require(const bool condition, const char *message) -> void {
    if (!condition) { throw std::runtime_error(message); }
}
template <typename Function>
static auto rejects(Function function) -> void {
    bool failed = false;
    try { function(); } catch (const std::exception &) { failed = true; }
    require(failed, "expected explicit rejection");
}
static auto view(const std::string_view bytes) -> ChunkView {
    return {reinterpret_cast<const std::uint8_t *>(bytes.data()), bytes.size()};
}
static auto noise(const std::size_t length, std::uint32_t seed) -> std::string {
    std::string bytes(length, '\0');
    for (auto &byte : bytes) {
        seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
        byte = static_cast<char>(seed & 255);
    }
    return bytes;
}
static auto testCodec() -> void {
    const auto base = noise(8192, 0x13579bdfU);
    std::vector<std::string> targets {base, "", std::string(4096, '\0'), noise(12000, 42)};
    for (const auto position : {std::size_t(0), base.size() / 2, base.size()}) {
        auto inserted = base; inserted.insert(position, std::string("new\0bytes", 9)); targets.push_back(inserted);
        auto erased = base; erased.erase(std::min(position, base.size() - 20), 20); targets.push_back(erased);
    }
    auto mutation = base; mutation[4000] ^= 0x55; targets.push_back(mutation);
    for (const auto &target : targets) {
        for (const auto &source : {base, std::string()}) {
            const auto delta = encodeDelta(view(source), view(target));
            require(delta.size() >= 5 && static_cast<unsigned char>(delta[0]) == 0xd6
                && static_cast<unsigned char>(delta[1]) == 0xc3 && static_cast<unsigned char>(delta[2]) == 0xc4
                && delta[3] == 0 && (delta[4] & 1) == 0, "VCDIFF header/secondary compression profile");
            require(decodeDelta(view(source), delta, target.size()) == target, "real Xdelta roundtrip");
        }
    }
    const auto delta = encodeDelta(view(base), view(mutation));
    require(delta.size() < mutation.size(), "similar target not compressed");
    rejects([&] { decodeDelta(view(noise(8192, 23)), delta, mutation.size()); });
    rejects([&] { decodeDelta(view(base), delta.substr(0, delta.size() - 1), mutation.size()); });
    auto corrupt = delta; corrupt.back() ^= 0x55;
    rejects([&] { decodeDelta(view(base), corrupt, mutation.size()); });
    rejects([&] { decodeDelta(view(base), delta, mutation.size() - 1); });
    rejects([&] { decodeDelta(view(base), delta, mutation.size() + 1); });
    rejects([&] { decodeDelta({nullptr, 1}, delta, mutation.size()); });
    rejects([&] { encodeDelta({nullptr, 1}, view(base)); });
    rejects([&] { encodeDelta(view(base), {nullptr, 1}); });
    rejects([&] { decodeDelta(view(base), "", 0); });
}
static auto testStore() -> void {
    EncodedStore store;
    auto base = noise(8192, 0x13579bdfU);
    auto target = base; target[4000] ^= 0x55;
    const auto baseDigest = sha256(base), targetDigest = sha256(target);
    require(appendEncodedChunk(store, 91, view(base), std::nullopt) == PayloadKind::Raw, "base Raw");
    require(appendEncodedChunk(store, 3, view(target), Candidate {0, 0, 1}) == PayloadKind::Delta, "target Delta");
    require(store.chunks[1].payload != target && store.chunks[1].payload.size() < target.size(), "Delta retains full target");
    base.clear(); base.shrink_to_fit(); target.clear(); target.shrink_to_fit();
    require(sha256(restoreEncodedChunk(store, 0)) == baseDigest && sha256(restoreEncodedChunk(store, 1)) == targetDigest,
        "restoration needs source oracle");
    require(appendEncodedChunk(store, 2, view("unrelated-12"), Candidate {0, 1, 2}) == PayloadKind::Raw, "negative gain did not fall back");
    require(!store.attempts.back().accepted && store.attempts.back().deltaBytes >= 12, "fallback attempt missing");
    require(restoreEncodedChunk(store, 2) == "unrelated-12", "Raw fallback payload lost");
    appendEncodedChunk(store, 4, view(""), std::nullopt);
    appendEncodedChunk(store, 5, view("short"), std::nullopt);
    require(restoreEncodedChunk(store, 3).empty() && restoreEncodedChunk(store, 4) == "short", "empty/short Raw");
    rejects([&] { appendEncodedChunk(store, 99, view("short"), Candidate {0, 0, 0}); });
    rejects([&] { appendEncodedChunk(store, 99, view(noise(100, 9)), Candidate {1, 0, 0}); });
    rejects([&] { appendEncodedChunk(store, 99, view(noise(100, 9)), Candidate {999, 0, 0}); });
    rejects([&] { restoreEncodedChunk(store, 99); });
    rejects([&] { appendEncodedChunk(store, 99, view(noise(100, 9)), Candidate {4, 0, 0}); });
    rejects([&] { appendEncodedChunk(store, 99, view(noise(100, 9)), Candidate {0, 3, 0}); });
    const auto intact = store;
    store.chunks[1].baseId = 1; rejects([&] { restoreEncodedChunk(store, 1); });
    store = intact; store.chunks[1].baseId = 2; rejects([&] { restoreEncodedChunk(store, 1); });
    store = intact; store.chunks[2] = store.chunks[1]; store.chunks[2].id = 2; store.chunks[2].baseId = 1;
    rejects([&] { restoreEncodedChunk(store, 2); });
    store = intact; store.chunks[0].payload[0] ^= 1; rejects([&] { restoreEncodedChunk(store, 1); });
    store = intact; store.chunks[1].payload.back() ^= 1; rejects([&] { restoreEncodedChunk(store, 1); });
    store = intact; store.chunks[1].payload.pop_back(); rejects([&] { restoreEncodedChunk(store, 1); });
    store = intact; store.chunks[1].originalLength++; rejects([&] { restoreEncodedChunk(store, 1); });
    store = intact; store.chunks[1].restoredSha256[0] ^= 1; rejects([&] { restoreEncodedChunk(store, 1); });
    store = intact; store.chunks[0].baseId = 0; rejects([&] { restoreEncodedChunk(store, 0); });
    store = intact; store.chunks[1].baseId.reset(); rejects([&] { restoreEncodedChunk(store, 1); });
    store = intact; store.chunks[1].id = 9; rejects([&] { restoreEncodedChunk(store, 1); });
}
static auto testPipeline() -> void {
    std::array<EncodedStore, 2> stores;
    std::vector<std::string> expected;
    std::vector<StoredChunkId> ids;
    {
        ExperimentInput input;
        // 不存在任何可读源文件；恢复不得尝试从 path 借回原文。
        input.files.push_back({"/no-such-finesse-delta-source", 0, "not-used"});
        const auto base = noise(8192, 0x13579bdfU);
        auto a = base; a[100] ^= 1;
        auto b = base; b[101] ^= 2;
        expected = {base, a, b, a, "short", "short", ""};
        for (std::size_t i = 0; i < expected.size(); ++i) {
            const auto result = storeChunk(input.store, view(expected[i]));
            if (!result.isDuplicate) { input.uniqueChunkPositions.push_back(input.chunks.size()); }
            input.chunks.push_back({100 - i, 0, 0, expected[i].size(), result.storedId, result.isDuplicate});
            ids.push_back(result.storedId);
        }
        stores[0] = buildEncodedStore(input, "Finesse-Gear");
        stores[1] = buildEncodedStore(input, "NTransform-Gear");
        rejects([&] { buildEncodedStore(input, "unknown"); });
    } // Store 原文和临时源数据全部销毁
    for (const auto &store : stores) {
        require(store.chunks.size() == 5 && ids[1] == ids[3] && ids[4] == ids[5], "duplicate encoding repeated");
        require(store.chunks[1].kind == PayloadKind::Delta && store.chunks[2].kind == PayloadKind::Delta, "fixture missing Delta");
        require(store.chunks[1].baseId == 0 && store.chunks[2].baseId == 0, "Delta entered Raw-only index");
        for (std::size_t i = 0; i < ids.size(); ++i) {
            require(restoreEncodedChunk(store, ids[i]) == expected[i], "released-oracle restore mismatch");
        }
    }
    auto index = makeResemblanceIndex("Finesse-Gear", PROFILE_ID);
    SignatureResult signature; signature.status = ExtractStatus::Ok; signature.superFeatures = {77, 88, 99};
    require(!queryCandidate(index, 100, signature, "Finesse-Gear", PROFILE_ID), "empty index matched");
    require(index.entries.empty() && index.insertedIds.empty(), "query mutates index");
    insertSignature(index, 100, signature, "Finesse-Gear", PROFILE_ID);
    SignatureResult cross = signature; cross.superFeatures = {99, 77, 88};
    const auto candidate = queryCandidate(index, 2, cross, "Finesse-Gear", PROFILE_ID);
    require(candidate && candidate->chunkId == 100 && candidate->querySlot == 0 && candidate->historicalSlot == 2, "cross-slot FirstFit changed");
    insertSignature(index, 2, cross, "Finesse-Gear", PROFILE_ID);
    require(queryCandidate(index, 3, signature, "Finesse-Gear", PROFILE_ID)->chunkId == 100, "minimum ID replaced earliest insert");
    rejects([&] { insertSignature(index, 2, cross, "Finesse-Gear", PROFILE_ID); });
}
int main() {
    try {
        testCodec(); testStore(); testPipeline();
        std::cout << "Delta codec/store/pipeline tests passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
