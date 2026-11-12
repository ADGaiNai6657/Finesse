#include "ChunkStore.h"

#include <limits>
#include <stdexcept>

//检查 chunk 地址并转成字节串视图
//用于观察chunk中所存储的数据 (使用string_view观察,转化为十六进制数串)
//输入：chunk：字节地址和长度，非零长度必须有有效地址。
//输出：返回借用原缓冲的 string_view；空输入返回空视图，非法地址抛异常。
static auto contentView(const ChunkView chunk) -> std::string_view {
    if (chunk.length > 0 && chunk.data == nullptr) {
        throw std::invalid_argument("null chunk data with nonzero length");
    }
    return chunk.length == 0 ? std::string_view {} : std::string_view {
        reinterpret_cast<const char *>(chunk.data),
        chunk.length
    };
}

//为唯一块池安全累加计数或字节数
//输入：left/right：两个无符号 64 位计数。
//输出：返回两者之和；超出 uint64 范围时抛异常，不允许静默回绕。
static auto addCount(const std::uint64_t left, const std::uint64_t right) -> std::uint64_t {
    if (right > std::numeric_limits<std::uint64_t>::max() - left) {
        throw std::overflow_error("chunk store counter overflow");
    }
    return left + right;
}

//建立一次实验独占的空唯一原文池
//输入：hashFunction：内容摘要函数，生产使用 sha1，测试可注入碰撞函数。
//输出：返回空 ChunkStore；函数指针为空时抛异常。
auto makeChunkStore(const ChunkHashFunction hashFunction) -> ChunkStore {
    if (hashFunction == nullptr) {
        throw std::invalid_argument("null chunk hash function");
    }
    return {{}, {}, {}, hashFunction};
}

//按唯一 ID 读取池内原文记录
//输入：store：拥有记录的原文池；id：池内 storedId。
//输出：返回记录的只读引用；ID 越界抛异常，引用在池清空或销毁前有效。
auto readStoredChunk(const ChunkStore &store, const StoredChunkId id) -> const StoredChunk & {
    if (id >= store.chunks.size()) {
        throw std::out_of_range("invalid stored chunk ID");
    }
    return store.chunks[static_cast<std::size_t>(id)];
}

//为池内唯一原文生成只读字节视图
//输入：store：原文池；id：唯一记录 ID。
//输出：返回借用记录 payload 的 ChunkView；池清空或销毁后视图失效，非法 ID 抛异常。
auto storedChunkView(const ChunkStore &store, const StoredChunkId id) -> ChunkView {
    const auto &chunk = readStoredChunk(store, id);
    return {reinterpret_cast<const std::uint8_t *>(chunk.bytes.data()), chunk.bytes.size()};
}

//在同摘要候选中用长度和全部字节确认重复
//输入：store：原文池；hash：待查 SHA-1；content：待比较的完整内容。
//输出：返回完全相同内容的 storedId，没有则返回空；不改变池或计数。
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

//只查询 chunk 是否已有完全相同的内容
//输入：store：原文池及其摘要函数；chunk：待查只读字节。
//输出：返回可选 storedId；按摘要、长度和全部字节确认，不插入、不增加引用计数，非法输入抛异常。
auto findDuplicate(const ChunkStore &store, const ChunkView chunk) -> std::optional<StoredChunkId> {
    if (store.hashFunction == nullptr) {
        throw std::invalid_argument("null chunk hash function");
    }
    const auto content = contentView(chunk);
    return findInBucket(store, store.hashFunction(content), content);
}

//将新内容入池，完全重复的内容只增加引用
//输入：store：待更新的原文池；chunk：调用期间有效的只读字节。
//输出：返回 storedId 和重复标记，并更新计数；仅新内容复制 payload，错误或计数溢出抛异常。
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

//清空唯一原文、摘要索引及全部计数
//输入：store：要重置的原文池。
//输出：无返回值；清空池但保留摘要函数，之前借用的视图和引用全部失效。
auto resetStore(ChunkStore &store) -> void {
    store.index.clear();
    store.chunks.clear();
    store.stats = {};
}
