#include "ChunkStore/ChunkStore.h"

#include <iostream>
#include <limits>
#include <stdexcept>

static auto require(const bool condition, const char *message) -> void {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

template <typename Function>
static auto rejects(Function function) -> void {
    bool failed = false;
    try { function(); } catch (const std::exception &) { failed = true; }
    require(failed, "expected explicit rejection");
}

static auto view(const std::string_view content) -> ChunkView {
    return {reinterpret_cast<const std::uint8_t *>(content.data()), content.size()};
}

static auto sameHash(std::string_view) -> Sha1Digest { return {}; }

static auto testHash() -> void {
    require(sha1Hex(sha1("")) == "da39a3ee5e6b4b0d3255bfef95601890afd80709", "SHA-1 empty KAT");
    require(sha1Hex(sha1("abc")) == "a9993e364706816aba3e25717850c26c9cd0d89d", "SHA-1 abc KAT");
    const std::string binary("a\0b", 3);
    require(sha1(binary) != sha1("a"), "binary length ignored");
}

static auto testStorage() -> void {
    auto store = makeChunkStore(sha1);
    std::string source("same\0payload", 12);
    const auto saved = source;
    const auto first = storeChunk(store, view(source));
    const auto pointer = storedChunkView(store, first.storedId).data;
    source.assign("changed");
    for (std::size_t i = 0; i < 100; ++i) {
        const auto duplicate = storeChunk(store, view(saved));
        require(duplicate.isDuplicate && duplicate.storedId == first.storedId, "duplicate payload copied");
        require(storedChunkView(store, duplicate.storedId).data == pointer, "duplicate backing memory differs");
    }
    require(store.chunks.size() == 1 && store.stats.uniqueBytes == saved.size(), "more than one unique payload");
    require(readStoredChunk(store, 0).bytes == saved && readStoredChunk(store, 0).referenceCount == 101, "ownership/reference count");
    require(store.stats.totalChunks == 101 && store.stats.duplicateChunks == 100, "counts");
    require(store.stats.totalBytes == saved.size() * 101 && store.stats.duplicateBytes == saved.size() * 100, "byte counts");
    const auto before = store.stats.totalChunks;
    require(findDuplicate(store, view(saved)) == first.storedId && !findDuplicate(store, view("missing")), "exact lookup");
    require(store.stats.totalChunks == before, "lookup mutates statistics");
    for (std::size_t i = 0; i < 1000; ++i) {
        storeChunk(store, view(std::to_string(i)));
    }
    require(storedChunkView(store, 0).data == pointer && readStoredChunk(store, 0).bytes == saved, "append invalidated view");
    auto isolated = makeChunkStore(sha1);
    require(!storeChunk(isolated, view(saved)).isDuplicate, "store isolation");
    rejects([&] { storeChunk(store, {nullptr, 1}); });
    rejects([&] { readStoredChunk(store, std::numeric_limits<StoredChunkId>::max()); });
    rejects([] { makeChunkStore(nullptr); });
    resetStore(store);
    require(store.chunks.empty() && store.index.empty() && store.stats.totalBytes == 0, "reset failed");
    require(!storeChunk(store, view(saved)).isDuplicate, "reset retained old content");
}

static auto testCollisions() -> void {
    auto store = makeChunkStore(sameHash);
    const auto a = storeChunk(store, view("AAAA"));
    const auto b = storeChunk(store, view("BBBB"));
    const auto c = storeChunk(store, view("longer"));
    require(!a.isDuplicate && !b.isDuplicate && !c.isDuplicate && store.chunks.size() == 3, "digest collision merged content");
    require(storeChunk(store, view("BBBB")).storedId == b.storedId, "collision bucket lookup");
    require(storeChunk(store, view("AAAA")).storedId == a.storedId, "collision bucket order");
    require(findDuplicate(store, view("longer")) == c.storedId && !findDuplicate(store, view("CCCC")), "collision exact lookup");
    const auto empty = storeChunk(store, {nullptr, 0});
    require(!empty.isDuplicate && storeChunk(store, {nullptr, 0}).isDuplicate, "empty chunk storage");
    const auto previousChunks = store.chunks.size();
    store.stats.totalBytes = std::numeric_limits<std::uint64_t>::max();
    rejects([&] { storeChunk(store, view("overflow")); });
    require(store.chunks.size() == previousChunks, "failed insertion changed storage");
}

int main() {
    try {
        testHash(); testStorage(); testCollisions();
        std::cout << "Chunk store: SHA-1 KAT, exact dedup, shared storage, collision, ownership and reset checks passed.\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
