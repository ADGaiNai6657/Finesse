#include "BreakingApart/Chunker.h"
#include "Common/Types.h"
#include "Experiment/IO.h"
#include "Finesse/Finesse.h"
#include "GearHash/GearHash.h"
#include "NTransform/NTransform.h"
#include "Pipeline/Pipeline.h"
#include "SuperFeature/SuperFeature.h"

#include <algorithm>
#include <array>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

// 不依赖 assert，Release/NDEBUG 下仍运行全部检查。
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
    bool rejected = false;
    try { function(); } catch (const std::exception &) { rejected = true; }
    require(rejected, "expected explicit rejection");
}

//用直观递推计算测试用 Gear 参考值
//输入：bytes/length：有效字节缓冲及长度；hash：可选初始状态，默认 0。
//输出：返回逐字节乘 2 加表值的 uint64 结果，不调用生产滚动函数。
static auto referenceHash(const std::uint8_t *bytes,
                          const std::size_t length,
                          std::uint64_t hash = 0) -> std::uint64_t {
    for (std::size_t i = 0; i < length; ++i) {
        hash = hash * std::uint64_t {2} + GEAR_TABLE[bytes[i]];
    }
    return hash;
}

//独立编码四个 feature 并计算 SF 参考值
//输入：values：四个按序 uint64 feature。
//输出：返回小端字节串的参考 Gear 指纹，供核对生产 SF 编码与哈希。
static auto referenceSF(const FeatureGroup &values) -> std::uint64_t {
    std::vector<std::uint8_t> bytes;
    for (auto value : values) {
        for (std::size_t i = 0; i < FEATURE_BYTES; ++i) {
            bytes.push_back(static_cast<std::uint8_t>(value % 256));
            value /= 256;
        }
    }
    return referenceHash(bytes.data(), bytes.size());
}

//用预存 Gear 序列计算两算法的朴素参考签名
//输入：bytes：测试字节数组，调用者保证长度至少 12；finesse：true 选择 Finesse，false 选择 NTransform。
//输出：返回成功状态、features 和 SF，供与生产提取结果逐项比较。
static auto referenceSignature(const std::vector<std::uint8_t> &bytes, const bool finesse) -> SignatureResult {
    std::vector<std::uint64_t> fingerprints;
    std::uint64_t hash = 0;
    for (const auto byte : bytes) {
        hash = hash * std::uint64_t {2} + GEAR_TABLE[byte];
        fingerprints.push_back(hash);
    }
    SignatureResult result;
    result.status = ExtractStatus::Ok;
    FeatureGroups groups {};
    if (finesse) {
        for (std::size_t i = 0; i < FEATURE_COUNT; ++i) {
            // 测试缓冲小于 64 KiB，直接乘法与生产商余数实现独立。
            const auto begin = i * bytes.size() / FEATURE_COUNT;
            const auto end = (i + 1) * bytes.size() / FEATURE_COUNT;
            result.features[i] = *std::max_element(fingerprints.begin() + begin, fingerprints.begin() + end);
        }
        for (std::size_t group = 0; group < FEATURES_PER_SUPER_FEATURE; ++group) {
            std::vector<FingerprintValue> sorted(result.features.begin() + group * SUPER_FEATURE_COUNT,
                result.features.begin() + (group + 1) * SUPER_FEATURE_COUNT);
            std::stable_sort(sorted.begin(), sorted.end(), std::greater<>());
            for (std::size_t rank = 0; rank < SUPER_FEATURE_COUNT; ++rank) {
                groups[rank][group] = sorted[rank];
            }
        }
    } else {
        const auto parameters = approvedTransformParameters();
        for (std::size_t i = 0; i < FEATURE_COUNT; ++i) {
            for (const auto fp : fingerprints) {
                // uint32_t 逐步模运算，独立于生产 uint64_t 乘加路径。
                auto value = static_cast<std::uint32_t>(fp);
                value *= parameters.multipliers[i];
                value += parameters.addends[i];
                result.features[i] = std::max(result.features[i], static_cast<FingerprintValue>(value));
            }
            groups[i / FEATURES_PER_SUPER_FEATURE][i % FEATURES_PER_SUPER_FEATURE] = result.features[i];
        }
    }
    for (std::size_t s = 0; s < SUPER_FEATURE_COUNT; ++s) {
        result.superFeatures[s] = referenceSF(groups[s]);
    }
    return result;
}

//检查两个提取结果的所有字段是否相同
//输入：left/right：待比较的 SignatureResult。
//输出：返回状态、features 和 SF 是否都相等，不修改结果。
static auto same(const SignatureResult &left, const SignatureResult &right) -> bool {
    return left.status == right.status && left.features == right.features && left.superFeatures == right.superFeatures;
}

//验证 Gear 已知答案、分批更新、空输入和 uint64 回绕
//输入：无，在函数内构造固定测试数据和错误场景。
//输出：无返回值；检查全部通过则正常结束，任何不符都抛异常使测试失败。
static auto testGear() -> void {
    std::array<std::uint8_t, 65> bytes {};
    for (std::size_t i = 0; i < bytes.size(); ++i) { bytes[i] = static_cast<std::uint8_t>(i); }
    const std::array<std::size_t, 5> lengths {0, 1, 63, 64, 65};
    const std::array<std::uint64_t, 5> known {0,
                                              0xe467a339562cde78ULL,
                                              0x4d1be82ba8f7b21bULL,
                                              0x470f62c58c91e259ULL,
                                              0x3f304f7800f06bd2ULL};
    for (std::size_t i = 0; i < lengths.size(); ++i) {
        std::uint64_t hash = 0;
        require(updateGearHash(bytes.data(), lengths[i], hash) == known[i], "Gear known answer");
        require(hash == referenceHash(bytes.data(), lengths[i]), "Gear reference mismatch");
        hash = 0;
        const auto split = lengths[i] / 2;
        updateGearHash(bytes.data(), split, hash);
        updateGearHash(bytes.data() + split, lengths[i] - split, hash);
        require(hash == known[i], "Gear split update mismatch");
    }
    std::uint64_t hash = 123;
    require(updateGearHash(nullptr, 0, hash) == 123, "empty update changes state");
    require(gearRoll(std::numeric_limits<std::uint64_t>::max(), 255)
        == referenceHash(bytes.data() + 64, 0, std::numeric_limits<std::uint64_t>::max()) * 2 + GEAR_TABLE[255], "Gear wrap");
}

//验证 12 段边界覆盖、余数分配和极大长度
//输入：无，在函数内构造固定测试数据和错误场景。
//输出：无返回值；检查全部通过则正常结束，任何不符都抛异常使测试失败。
static auto testBoundaries() -> void {
    const std::array<std::size_t, FEATURE_COUNT + 1> expected {0, 1, 2, 3, 4, 5, 7, 8, 9, 10, 11, 12, 14};
    for (std::size_t i = 0; i <= FEATURE_COUNT; ++i) {
        require(subchunkBoundary(14, i) == expected[i], "D-04 length 14");
    }
    for (const auto length : {std::size_t {12}, std::size_t {13}, std::size_t {8192},
                              std::numeric_limits<std::size_t>::max()}) {
        std::size_t total = 0;
        std::size_t smallest = length;
        std::size_t largest = 0;
        for (std::size_t i = 0; i < FEATURE_COUNT; ++i) {
            const auto begin = subchunkBoundary(length, i);
            const auto end = subchunkBoundary(length, i + 1);
            require(end > begin && end <= length, "invalid subchunk range");
            total += end - begin;
            smallest = std::min(smallest, end - begin);
            largest = std::max(largest, end - begin);
        }
        require(total == length && largest - smallest <= 1, "subchunk coverage");
    }
    rejects([] { subchunkBoundary(14, FEATURE_COUNT + 1); });
}

//验证两算法分组、稳定排名及 SF 小端编码
//输入：无，在函数内构造固定测试数据和错误场景。
//输出：无返回值；检查全部通过则正常结束，任何不符都抛异常使测试失败。
static auto testGrouping() -> void {
    const FeatureValues figure {0xff9ab74e, 0xfff82845, 0xfff1da26, 0xff7f156c, 0xff12c814, 0xffe3735c,
        0xff32bd8e, 0xfff9b8d2, 0xffe87e52, 0xfff93729, 0xffe2fcaf, 0xff16ecf3};
    const auto original = figure;
    const auto groups = groupFinesseFeatures(figure);
    require(groups[0] == FeatureGroup {figure[1], figure[5], figure[7], figure[9]}, "Figure 3 SF0");
    require(groups[1] == FeatureGroup {figure[2], figure[3], figure[8], figure[10]}, "Figure 3 SF1");
    require(groups[2] == FeatureGroup {figure[0], figure[4], figure[6], figure[11]}, "Figure 3 SF2");
    require(figure == original, "grouping modified input");
    FeatureValues equal;
    equal.fill(42);
    for (const auto &group : groupFinesseFeatures(equal)) { require(group == FeatureGroup {42,42,42,42}, "ties"); }
    FeatureValues sequence {};
    for (std::size_t i = 0; i < sequence.size(); ++i) { sequence[i] = i; }
    require(groupFinesseFeatures(sequence)[0] == FeatureGroup {2,5,8,11}, "ordered grouping");
    require(groupNTransformFeatures(sequence)[1] == FeatureGroup {4,5,6,7}, "baseline contiguous grouping");
    const FeatureGroup values {0x0807060504030201ULL, 0, 0xffffffffULL, 0xffffffffffffffffULL};
    const auto bytes = serializeFeatures(values);
    for (std::size_t b = 0; b < FEATURE_BYTES; ++b) {
        require(bytes[b] == b + 1 && bytes[8 + b] == 0 && bytes[24 + b] == 255, "SF byte encoding");
        require(bytes[16 + b] == (b < 4 ? 255 : 0), "32-bit zero extension");
    }
    require(hashSuperFeatures({values, values, values})[0] == referenceSF(values), "SF reference hash");
}

//将两算法与参考签名逐项比较并检查非法输入和状态隔离
//输入：无，在函数内构造固定测试数据和错误场景。
//输出：无返回值；检查全部通过则正常结束，任何不符都抛异常使测试失败。
static auto testExtraction() -> void {
    const HashConfig config;
    const auto parameters = approvedTransformParameters();
    std::vector<std::uint8_t> bytes(65537, 0);
    std::uint32_t random = 0x13579bdf;
    for (auto &byte : bytes) {
        random ^= random << 13; random ^= random >> 17; random ^= random << 5;
        byte = static_cast<std::uint8_t>(random);
    }
    for (std::size_t n = 0; n < FEATURE_COUNT; ++n) {
        const ChunkView view {bytes.data(), n};
        for (const auto &result : {extractFinesse(view, config), extractNTransform(view, config, parameters)}) {
            require(result.status == ExtractStatus::UnsupportedLength && result.features == FeatureValues {}
                && result.superFeatures == SuperFeatures {}, "short chunk contract");
        }
    }
    require(extractFinesse({nullptr, 0}, config).status == ExtractStatus::UnsupportedLength, "empty chunk");
    require(extractNTransform({nullptr, 12}, config, parameters).status == ExtractStatus::InvalidInput, "null baseline");
    require(extractFinesse({nullptr, 12}, config).status == ExtractStatus::InvalidInput, "null finesse");
    auto invalid = config;
    invalid.resetAtSubchunk = true;
    require(extractFinesse({bytes.data(), 12}, invalid).status == ExtractStatus::InvalidConfig, "invalid config");
    auto invalidParameters = parameters;
    invalidParameters.multipliers[0] ^= 2;
    require(extractNTransform({bytes.data(), 12}, config, invalidParameters).status == ExtractStatus::InvalidConfig, "invalid parameters");
    for (const auto length : {12,13,14,24,63,64,65,4096,8192,65537}) {
        const std::vector<std::uint8_t> subset(bytes.begin(), bytes.begin() + length);
        const ChunkView view {subset.data(), subset.size()};
        const auto finesse = extractFinesse(view, config);
        const auto baseline = extractNTransform(view, config, parameters);
        require(same(finesse, referenceSignature(subset, true)), "Finesse reference mismatch");
        require(same(baseline, referenceSignature(subset, false)), "NTransform reference mismatch");
        extractFinesse({bytes.data() + 7, 99}, config);
        extractNTransform({bytes.data() + 7, 99}, config, parameters);
        require(same(finesse, extractFinesse(view, config)), "Finesse state leak");
        require(same(baseline, extractNTransform(view, config, parameters)), "NTransform state leak");
    }
    for (const auto fill : {0,255}) {
        const std::vector<std::uint8_t> flat(100, static_cast<std::uint8_t>(fill));
        require(same(extractFinesse({flat.data(),flat.size()}, config), referenceSignature(flat, true)), "constant data");
    }
    require(transformFingerprint(0xffffffffffffffffULL, 0xffffffffU, 0xffffffffU) == 0, "multiply-add wrap boundary");
    require(transformFingerprint(0x1234567800000001ULL, 0xffffffffU, 2) == 1, "low 32 bits only");
}

//验证 FastCDC 切点、空文件、短尾块和配置拒绝
//输入：无，在函数内构造固定测试数据和错误场景。
//输出：无返回值；检查全部通过则正常结束，任何不符都抛异常使测试失败。
static auto testCDC() -> void {
    const auto config = makeFastCDCConfig(8192,2048,0);
    require(config.MaskA == 0x0000d90303530000ULL && config.MaskS == config.MaskA
        && config.MaskL == config.MaskA && config.MaxSize == 65536, "CDC defaults");
    require(chunkBoundaries("",config).empty(), "empty CDC");
    require(chunkBoundaries("abc",config) == std::vector<std::size_t> {3}, "short tail");
    std::string bytes(200003, '\0');
    auto cuts = chunkBoundaries(bytes, config);
    require(cuts == std::vector<std::size_t> {65536,131072,196608,200003}, "forced max chunks");
    std::uint32_t random = 0x13579bdf;
    for (auto &byte : bytes) {
        random ^= random << 13; random ^= random >> 17; random ^= random << 5;
        byte = static_cast<char>(random & 255U);
    }
    std::vector<std::vector<std::size_t>> original {{123}};
    BoundariesFinder(bytes, original, config);
    require(original.front() == std::vector<std::size_t> {123} && original.back() == chunkBoundaries(bytes, config), "CDC source comparison");
    // 独立逐段参考：判定字节 i 不进入当前块，下一块从 i 开始。
    std::vector<std::size_t> reference;
    std::size_t begin = 0;
    while (begin < bytes.size()) {
        const auto end = std::min(bytes.size(), begin + config.MaxSize);
        auto cut = end;
        std::uint64_t hash = 0;
        for (auto i = begin + config.MinSize; i < end; ++i) {
            hash = hash * 2 + GEAR_TABLE[static_cast<unsigned char>(bytes[i])];
            if ((hash & config.MaskA) == 0) { cut = i; break; }
        }
        reference.push_back(cut); begin = cut;
    }
    require(reference == original.back(), "CDC independent reference");
    std::size_t trigger = 0;
    while (trigger < 256 && (GEAR_TABLE[trigger] & config.MaskA) != 0) { ++trigger; }
    if (trigger < 256) {
        bytes.assign(4096, '\0'); bytes[2048] = static_cast<char>(trigger);
        require(chunkBoundaries(bytes,config).front() == 2048, "CDC i vs i+1 semantics");
    }
    auto invalid = config; invalid.MinSize = 0;
    rejects([&] { chunkBoundaries("x",invalid); });
    invalid = config; invalid.NcLevel = 4;
    rejects([&] { chunkBoundaries("x",invalid); });
}

//验证跨槽 FirstFit、最早插入及算法/配置隔离
//输入：无，在函数内构造固定测试数据和错误场景。
//输出：无返回值；检查全部通过则正常结束，任何不符都抛异常使测试失败。
static auto testIndex() -> void {
    auto index = makeResemblanceIndex("Finesse-Gear", PROFILE_ID);
    const auto signature = [](SuperFeatures sf) { return SignatureResult {ExtractStatus::Ok, {}, sf}; };
    require(!queryThenInsert(index, 9, signature({44,55,11}), "Finesse-Gear", PROFILE_ID), "self match");
    const auto candidate = queryThenInsert(index, 7, signature({11,22,33}), "Finesse-Gear", PROFILE_ID);
    require(candidate && candidate->chunkId == 9 && candidate->querySlot == 0 && candidate->historicalSlot == 2, "cross-slot");
    const auto next = queryThenInsert(index, 20, signature({11,11,11}), "Finesse-Gear", PROFILE_ID);
    require(next && next->chunkId == 9, "earliest insertion is not smallest ID");
    rejects([&] { queryThenInsert(index,20,signature({1,2,3}),"Finesse-Gear",PROFILE_ID); });
    rejects([&] { queryThenInsert(index,21,SignatureResult {},"Finesse-Gear",PROFILE_ID); });
    rejects([&] { queryThenInsert(index,21,signature({11,1,1}),"NTransform-Gear",PROFILE_ID); });
    auto other = makeResemblanceIndex("NTransform-Gear",PROFILE_ID);
    require(!queryThenInsert(other,0,signature({11,11,11}),"NTransform-Gear",PROFILE_ID), "index isolation");
    const auto tied = queryThenInsert(other,1,signature({11,22,33}),"NTransform-Gear",PROFILE_ID);
    require(tied && tied->historicalSlot == 0, "historical lowest tied slot");
}

//验证 CSV、UTF-8、SHA-256、整数解析和溢出检查
//输入：无，在函数内构造固定测试数据和错误场景。
//输出：无返回值；检查全部通过则正常结束，任何不符都抛异常使测试失败。
static auto testIO() -> void {
    const CsvRows rows {{"a", "b", "c"}, {"one,comma", "quote\"x", "multi\nline"}, {"中文", "", "x\r\ny"}};
    require(parseCsv(encodeCsv(rows)) == rows, "CSV round trip");
    require(parseCsv("a,b\r\n1,2\r\n").size() == 2, "CRLF CSV");
    rejects([] { parseCsv("\"unfinished"); });
    rejects([] { parseCsv("\"done\"x"); });
    rejects([] { parseCsv("unquoted\"x"); });
    rejects([] { requireUtf8(std::string("\xc0\x80",2)); });
    rejects([] { parseUnsigned("-1"); });
    rejects([] { parseUnsigned("18446744073709551616"); });
    rejects([] { checkedAdd(std::numeric_limits<std::uint64_t>::max(),1); });
    rejects([] { checkedMultiply(std::numeric_limits<std::uint64_t>::max(),2); });
    require(sha256("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "SHA-256 known answer");
}

//执行本文件全部 C++ 正确性测试
//输入：无显式输入，使用本文件构造的固定测试数据。
//输出：返回 0 表示全部通过；捕获异常后打印原因并返回 1。
int main() {
    try {
        testGear(); testBoundaries(); testGrouping(); testExtraction(); testCDC(); testIndex(); testIO();
        std::cout << "Core checks passed (T-01..T-11 and CDC adaptation).\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
