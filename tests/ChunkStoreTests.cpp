#include "ChunkStore/ChunkStore.h"

#include <iostream>
#include <limits>
#include <stdexcept>

//检查测试条件，确保 Release 下也不会被关闭
//输入：condition：必须成立的条件；message：条件不成立时的说明。
//输出：无返回值；条件为假时抛 runtime_error，让测试显式失败。
static auto require(const bool condition, const char *message) -> void {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

//检查被测操作是否会显式拒绝错误输入
//输入：function：无参数可调用对象，内部执行待检查操作。
//输出：无返回值；捕获 std::exception 视为预期拒绝，未抛异常则使测试失败。
template <typename Function>
static auto rejects(Function function) -> void {
    bool failed = false;
    try { function(); } catch (const std::exception &) { failed = true; }
    require(failed, "expected explicit rejection");
}

//将字符串字节转换成只读 chunk 视图
//输入：content：调用期间有效的字节串。
//输出：返回借用该串内存的 ChunkView，不复制 payload。
static auto view(const std::string_view content) -> ChunkView {
    return {reinterpret_cast<const std::uint8_t *>(content.data()), content.size()};
}

//为摘要碰撞测试强制制造相同摘要
//输入：任意字节串，刻意忽略其内容。
//输出：返回全零的 20 字节摘要，仅用于验证碰撞后的长度和字节确认。
static auto sameHash(std::string_view) -> Sha1Digest { return {}; }

//验证 SHA-1 已知答案、零字节和完整长度处理
//输入：无，在函数内构造固定测试数据和错误场景。
//输出：无返回值；检查全部通过则正常结束，任何不符都抛异常使测试失败。
static auto testHash() -> void {
    require(sha1Hex(sha1("")) == "da39a3ee5e6b4b0d3255bfef95601890afd80709", "SHA-1 empty KAT");
    require(sha1Hex(sha1("abc")) == "a9993e364706816aba3e25717850c26c9cd0d89d", "SHA-1 abc KAT");
    constexpr std::string binary("a\0b", 3);
    require(sha1(binary) != sha1("a"), "binary length ignored");
}

//验证唯一原文所有权、重复引用、地址稳定、计数和重置
//输入：无，在函数内构造固定测试数据和错误场景。
//输出：无返回值；检查全部通过则正常结束，任何不符都抛异常使测试失败。
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

//验证强制同摘要的不同内容仍独立存储及错误计数边界
//输入：无，在函数内构造固定测试数据和错误场景。
//输出：无返回值；检查全部通过则正常结束，任何不符都抛异常使测试失败。
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

//执行本文件全部 C++ 正确性测试
//输入：无显式输入，使用本文件构造的固定测试数据。
//输出：返回 0 表示全部通过；捕获异常后打印原因并返回 1。
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
