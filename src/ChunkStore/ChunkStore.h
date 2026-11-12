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

//建立一次实验独占的空唯一原文池
//输入：hashFunction：内容摘要函数，生产使用 sha1，测试可注入碰撞函数。
//输出：返回空 ChunkStore；函数指针为空时抛异常。
auto makeChunkStore(ChunkHashFunction hashFunction) -> ChunkStore;
//只查询 chunk 是否已有完全相同的内容
//输入：store：原文池及其摘要函数；chunk：待查只读字节。
//输出：返回可选 storedId；按摘要、长度和全部字节确认，不插入、不增加引用计数，非法输入抛异常。
auto findDuplicate(const ChunkStore &store, ChunkView chunk) -> std::optional<StoredChunkId>;
//将新内容入池，完全重复的内容只增加引用
//输入：store：待更新的原文池；chunk：调用期间有效的只读字节。
//输出：返回 storedId 和重复标记，并更新计数；仅新内容复制 payload，错误或计数溢出抛异常。
auto storeChunk(ChunkStore &store, ChunkView chunk) -> ChunkStoreResult;
//按唯一 ID 读取池内原文记录
//输入：store：拥有记录的原文池；id：池内 storedId。
//输出：返回记录的只读引用；ID 越界抛异常，引用在池清空或销毁前有效。
auto readStoredChunk(const ChunkStore &store, StoredChunkId id) -> const StoredChunk &;
//为池内唯一原文生成只读字节视图
//输入：store：原文池；id：唯一记录 ID。
//输出：返回借用记录 payload 的 ChunkView；池清空或销毁后视图失效，非法 ID 抛异常。
auto storedChunkView(const ChunkStore &store, StoredChunkId id) -> ChunkView;
// 清空后此前的视图/引用全部失效；保留所选哈希函数。
//清空唯一原文、摘要索引及全部计数
//输入：store：要重置的原文池。
//输出：无返回值；清空池但保留摘要函数，之前借用的视图和引用全部失效。
auto resetStore(ChunkStore &store) -> void;
