#include "ChunkStore.h"

#include <limits>
#include <stdexcept>

static auto contentView(const ChunkView chunk) -> std::string_view {
    if (chunk.length > 0 && chunk.data == nullptr) {
        throw std::invalid_argument("null chunk data with nonzero length");
    }
    return chunk.length == 0 ? std::string_view {} : std::string_view {
        reinterpret_cast<const char *>(chunk.data), chunk.length};
}

static auto addCount(const std::uint64_t left, const std::uint64_t right) -> std::uint64_t {
    if (right > std::numeric_limits<std::uint64_t>::max() - left) {
        throw std::overflow_error("chunk store counter overflow");
    }
    return left + right;
}

auto makeChunkStore(const ChunkHashFunction hashFunction) -> ChunkStore {
    if (hashFunction == nullptr) {
        throw std::invalid_argument("null chunk hash function");
    }
    return {{}, {}, {}, hashFunction};
}

auto readStoredChunk(const ChunkStore &store, const StoredChunkId id) -> const StoredChunk & {
    if (id >= store.chunks.size()) {
        throw std::out_of_range("invalid stored chunk ID");
    }
    return store.chunks[static_cast<std::size_t>(id)];
}

auto storedChunkView(const ChunkStore &store, const StoredChunkId id) -> ChunkView {
    const auto &chunk = readStoredChunk(store, id);
    return {reinterpret_cast<const std::uint8_t *>(chunk.bytes.data()), chunk.bytes.size()};
}

static auto findInBucket(const ChunkStore &store, const Sha1Digest &hash, const std::string_view content)
    -> std::optional<StoredChunkId> {
    const auto range = store.index.equal_range(hash);
    for (auto found = range.first; found != range.second; ++found) {
        const auto &candidate = readStoredChunk(store, found->second);
        // 同一 SHA-1 可以对应多个不同内容，长度和所有字节相等后才确认重复。
        if (candidate.bytes.size() == content.size() && candidate.bytes == content) {
            return candidate.id;
        }
    }
    return std::nullopt;
}

auto findDuplicate(const ChunkStore &store, const ChunkView chunk) -> std::optional<StoredChunkId> {
    if (store.hashFunction == nullptr) {
        throw std::invalid_argument("null chunk hash function");
    }
    const auto content = contentView(chunk);
    return findInBucket(store, store.hashFunction(content), content);
}

auto storeChunk(ChunkStore &store, const ChunkView chunk) -> ChunkStoreResult {
    if (store.hashFunction == nullptr) {
        throw std::invalid_argument("null chunk hash function");
    }
    const auto content = contentView(chunk);
    const auto hash = store.hashFunction(content);
    const auto existing = findInBucket(store, hash, content);
    auto next = store.stats;
    next.totalChunks = addCount(next.totalChunks, 1);
    next.totalBytes = addCount(next.totalBytes, chunk.length);
    if (existing) {
        auto &stored = store.chunks[static_cast<std::size_t>(*existing)];
        const auto references = addCount(stored.referenceCount, 1);
        next.duplicateChunks = addCount(next.duplicateChunks, 1);
        next.duplicateBytes = addCount(next.duplicateBytes, chunk.length);
        stored.referenceCount = references;
        store.stats = next;
        return {*existing, true};
    }
    next.uniqueChunks = addCount(next.uniqueChunks, 1);
    next.uniqueBytes = addCount(next.uniqueBytes, chunk.length);
    const auto id = static_cast<StoredChunkId>(store.chunks.size());
    // 只在确认为新内容后复制 payload；重复出现只返回同一 storedId。
    store.chunks.push_back({id, hash, std::string(content), 1});
    try {
        store.index.emplace(hash, id);
    } catch (...) {
        store.chunks.pop_back();
        throw;
    }
    store.stats = next;
    return {id, false};
}

auto resetStore(ChunkStore &store) -> void {
    store.index.clear();
    store.chunks.clear();
    store.stats = {};
}
