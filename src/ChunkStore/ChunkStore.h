#pragma once

#include "Common/Types.h"
#include "Hash/Hash.h"

#include <deque>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

using StoredChunkId = std::uint64_t;
using ChunkHashFunction = auto (*)(std::string_view) -> Sha1Digest;

struct StoredChunk {
    StoredChunkId id = 0;
    Sha1Digest hash {};
    std::string bytes;
    std::uint64_t referenceCount = 0;
};

struct ChunkStoreStats {
    std::uint64_t totalChunks = 0;
    std::uint64_t uniqueChunks = 0;
    std::uint64_t duplicateChunks = 0;
    std::uint64_t totalBytes = 0;
    std::uint64_t uniqueBytes = 0;
    std::uint64_t duplicateBytes = 0;
};

// 每次实验独占。普通追加不移动旧块；bytes 入池后不可修改。
// hashFunction 的替换只供碰撞测试；实际实验显式使用 sha1。
struct ChunkStore {
    std::deque<StoredChunk> chunks;
    std::unordered_multimap<Sha1Digest, StoredChunkId, Sha1DigestHash> index;
    ChunkStoreStats stats;
    ChunkHashFunction hashFunction = sha1;
};

struct ChunkStoreResult {
    StoredChunkId storedId = 0;
    bool isDuplicate = false;
};

auto makeChunkStore(ChunkHashFunction hashFunction) -> ChunkStore;
auto findDuplicate(const ChunkStore &store, ChunkView chunk) -> std::optional<StoredChunkId>;
auto storeChunk(ChunkStore &store, ChunkView chunk) -> ChunkStoreResult;
auto readStoredChunk(const ChunkStore &store, StoredChunkId id) -> const StoredChunk &;
auto storedChunkView(const ChunkStore &store, StoredChunkId id) -> ChunkView;
// 清空后此前的视图/引用全部失效；保留所选哈希函数。
auto resetStore(ChunkStore &store) -> void;
