#include "Experiment.h"

#include "BreakingApart/Chunker.h"
#include "IO.h"

#include <algorithm>
#include <chrono>
#include <set>
#include <sstream>
#include <stdexcept>

//去掉配置行首尾的空格、制表符和回车
//输入：value：待处理的字符串。
//输出：返回裁剪后的副本，全部为空白时返回空串，不修改原串。
static auto trim(const std::string &value) -> std::string {
    const auto begin = value.find_first_not_of(" \t\r");
    return begin == std::string::npos ? "" : value.substr(begin, value.find_last_not_of(" \t\r") - begin + 1);
}

//读取并严格检查 SF 实验配置
//输入：path：配置文件路径，文件内相对路径以配置目录为基准。
//输出：返回配置值、解析后的输入和输出路径及轮数；未知、重复、缺失或未批准的配置抛异常。
auto loadExperimentConfig(const std::filesystem::path &path) -> ExperimentConfig {
    const std::set<std::string> keys {"profileId", "inputMode", "inputPath", "datasetKind", "datasetDescription",
        "outputDirectory", "runId", "expectedSize", "minSize", "maxSize", "ncLevel", "warmupRuns",
        "measurementRuns", "iterations", "diagnostics", "sfInput"};
    ExperimentConfig config;
    config.configPath = std::filesystem::canonical(path);
    const auto bytes = readFile(config.configPath);
    requireUtf8(bytes);
    std::istringstream input(bytes);
    std::string line;
    while (std::getline(input, line)) {
        line = trim(line);
        if (line.empty() || line.front() == '#') {
            continue;
        }
        const auto separator = line.find('=');
        if (separator == std::string::npos) {
            throw std::invalid_argument("configuration line has no '='");
        }
        const auto key = trim(line.substr(0, separator));
        const auto value = trim(line.substr(separator + 1));
        if (!keys.contains(key) || value.empty() || !config.values.emplace(key, value).second) {
            throw std::invalid_argument("unknown, empty or duplicate config key: " + key);
        }
    }
    if (config.values.size() != keys.size()) {
        throw std::invalid_argument("missing required configuration key");
    }
    const auto &v = config.values;
    if (v.at("profileId") != PROFILE_ID || (v.at("inputMode") != "raw" && v.at("inputMode") != "manifest")
        || (v.at("datasetKind") != "real" && v.at("datasetKind") != "synthetic")
        || v.at("expectedSize") != "8192" || v.at("minSize") != "2048" || v.at("maxSize") != "65536"
        || v.at("ncLevel") != "0" || v.at("warmupRuns") != "1" || v.at("measurementRuns") != "5"
        || (v.at("diagnostics") != "true" && v.at("diagnostics") != "false")) {
        throw std::invalid_argument("configuration differs from approved D-14/D-16/D-17");
    }
    if (v.at("sfInput") != "unique" && v.at("sfInput") != "all") {
        throw std::invalid_argument("sfInput must be unique or all (D-21)");
    }
    for (const unsigned char c : v.at("runId")) {
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) {
            throw std::invalid_argument("runId must use ASCII letters, digits, underscore or hyphen");
        }
    }
    config.iterations = parseUnsigned(v.at("iterations"));
    if (config.iterations == 0) {
        throw std::invalid_argument("iterations must be positive");
    }
    config.diagnostics = v.at("diagnostics") == "true";
    config.inputPath = pathFrom(config.configPath.parent_path(), v.at("inputPath"));
    config.outputDirectory = pathFrom(config.configPath.parent_path(), v.at("outputDirectory"));
    return config;
}

//加载或复用一个源文件并记录其完整性信息
//输入：input：待更新实验输入；path：源路径；indices：路径到文件下标映射；buffers：加载缓冲。
//输出：返回文件下标；新文件写入来源元信息、缓冲及映射，已加载文件直接复用，读取失败抛异常。
static auto loadSource(ExperimentInput &input, const std::filesystem::path &path,
                       std::map<std::filesystem::path, std::size_t> &indices,
                       std::vector<std::string> &buffers) -> std::size_t {
    const auto canonical = std::filesystem::canonical(path);
    if (!std::filesystem::is_regular_file(canonical)) {
        throw std::invalid_argument("input is not a regular file: " + path.string());
    }
    if (const auto found = indices.find(canonical); found != indices.end()) {
        return found->second;
    }
    auto bytes = readFile(canonical);
    const auto digest = sha256(bytes);
    const auto index = input.files.size();
    input.files.push_back({canonical, bytes.size(), digest});
    buffers.push_back(std::move(bytes));
    indices.emplace(canonical, index);
    return index;
}

//将一次 chunk 出现绑定到唯一原文池
//输入：input：待更新实验输入；chunk：出现的来源范围和 ID；bytes：所属完整源文件缓冲。
//输出：无返回值；更新唯一池、storedId、重复标记、首次位置和出现列表，累计去重耗时，错误抛异常。
static auto bindChunk(ExperimentInput &input, InputChunk chunk, const std::string &bytes) -> void {
    const ChunkView view {reinterpret_cast<const std::uint8_t *>(bytes.data() + chunk.offset),
                          static_cast<std::size_t>(chunk.length)};
    const auto start = std::chrono::steady_clock::now();
    const auto stored = storeChunk(input.store, view);
    input.dedupElapsedNs += std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - start).count();
    chunk.storedId = stored.storedId;
    chunk.isDuplicate = stored.isDuplicate;
    if (!stored.isDuplicate) {
        input.uniqueChunkPositions.push_back(input.chunks.size());
    }
    input.chunks.push_back(chunk);
}

//按 raw 或 manifest 加载、校验并精确去重输入
//输入：config：已检查的输入模式、路径和 CDC 配置。
//输出：返回来源元信息、全部出现、唯一原文池和计数；源缓冲在返回前释放，I/O、范围、摘要或计数错误抛异常。
auto loadExperimentInput(const ExperimentConfig &config) -> ExperimentInput {
    ExperimentInput input;
    std::map<std::filesystem::path, std::size_t> indices;
    // 仅加载阶段持有源文件缓冲；返回的 ExperimentInput 只保留唯一 payload。
    std::vector<std::string> buffers;
    const auto inputBytes = readFile(config.inputPath);
    requireUtf8(inputBytes);
    if (config.values.at("inputMode") == "raw") {
        std::vector<std::filesystem::path> paths;
        std::set<std::filesystem::path> seen;
        std::istringstream list(inputBytes);
        std::string line;
        while (std::getline(list, line)) {
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            if (line.empty()) {
                throw std::invalid_argument("empty path in raw file list");
            }
            const auto path = std::filesystem::canonical(pathFrom(config.inputPath.parent_path(), line));
            if (!seen.insert(path).second) {
                throw std::invalid_argument("duplicate input path: " + path.string());
            }
            paths.push_back(path);
        }
        std::sort(paths.begin(), paths.end());
        const auto cdc = makeFastCDCConfig(8192, 2048, 0);
        for (const auto &path : paths) {
            const auto index = loadSource(input, path, indices, buffers);
            const auto start = std::chrono::steady_clock::now();
            const auto cuts = chunkBoundaries(buffers[index], cdc);
            input.cdcElapsedNs += std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - start).count();
            std::size_t previous = 0;
            for (const auto cut : cuts) {
                bindChunk(input, {static_cast<ChunkId>(input.chunks.size()), index, previous, cut - previous}, buffers[index]);
                previous = cut;
            }
            // 已有块均由 Store 自持有，逐文件释放原始缓冲，避免保留重复内容。
            std::string {}.swap(buffers[index]);
        }
    } else {
        const auto rows = parseCsv(inputBytes);
        if (rows.empty() || rows.front() != std::vector<std::string> {"chunkId", "file", "offset", "length"}) {
            throw std::invalid_argument("invalid manifest CSV header");
        }
        std::set<ChunkId> ids;
        for (std::size_t i = 1; i < rows.size(); ++i) {
            const auto &row = rows[i];
            if (row.size() != 4) {
                throw std::invalid_argument("manifest CSV row must have four fields");
            }
            const auto id = parseUnsigned(row[0]);
            if (!ids.insert(id).second) {
                throw std::invalid_argument("duplicate chunk ID");
            }
            const auto index = loadSource(input, pathFrom(config.inputPath.parent_path(), row[1]), indices, buffers);
            const auto offset = parseUnsigned(row[2]);
            const auto length = parseUnsigned(row[3]);
            if (checkedAdd(offset, length) > input.files[index].length) {
                throw std::invalid_argument("manifest chunk exceeds source file");
            }
            bindChunk(input, {id, index, offset, length}, buffers[index]);
        }
        const auto checksums = parseCsv(readFile(config.inputPath.parent_path() / "checksums.csv"));
        if (checksums.empty() || checksums.front() != std::vector<std::string> {"file", "sha256"}) {
            throw std::invalid_argument("invalid checksums CSV header");
        }
        std::map<std::filesystem::path, std::string> expected;
        for (std::size_t i = 1; i < checksums.size(); ++i) {
            const auto &row = checksums[i];
            if (row.size() != 2 || row[1].size() != 64 || row[1].find_first_not_of("0123456789abcdef") != std::string::npos) {
                throw std::invalid_argument("invalid SHA-256 checksum row");
            }
            const auto path = std::filesystem::canonical(pathFrom(config.inputPath.parent_path(), row[0]));
            if (!expected.emplace(path, row[1]).second) {
                throw std::invalid_argument("duplicate checksum path");
            }
        }
        const auto manifestPath = std::filesystem::canonical(config.inputPath);
        // 清单不能同时充当源文件，校验集合须精确等于清单加全部引用源文件。
        if (indices.contains(manifestPath) || expected.size() != input.files.size() + 1
            || !expected.contains(manifestPath) || expected.at(manifestPath) != sha256(inputBytes)) {
            throw std::invalid_argument("manifest checksum missing, extra or mismatched");
        }
        for (const auto &file : input.files) {
            if (!expected.contains(file.path) || expected.at(file.path) != file.digest) {
                throw std::invalid_argument("source checksum missing or mismatched: " + file.path.string());
            }
        }
    }
    for (const auto &chunk : input.chunks) {
        input.inputBytes = checkedAdd(input.inputBytes, chunk.length);
        if (chunk.length < FEATURE_COUNT) {
            input.excludedBytes = checkedAdd(input.excludedBytes, chunk.length);
            input.excludedChunks = checkedAdd(input.excludedChunks, 1);
        }
    }
    if (input.store.stats.totalBytes != input.inputBytes || input.store.stats.totalChunks != input.chunks.size()
        || input.store.stats.uniqueChunks != input.uniqueChunkPositions.size()) {
        throw std::runtime_error("chunk store and input accounting differ");
    }
    return input;
}

//从出现记录定位池内唯一原文
//输入：input：拥有原文池的实验输入；chunk：带 storedId 和长度的出现记录。
//输出：返回借用池内 payload 的 ChunkView；非法 ID 或长度不一致抛异常，input 清空或销毁后失效。
auto inputChunkView(const ExperimentInput &input, const InputChunk &chunk) -> ChunkView {
    const auto view = storedChunkView(input.store, chunk.storedId);
    if (view.length != chunk.length) {
        throw std::runtime_error("stored payload length differs from occurrence");
    }
    return view;
}

//为 SF 微基准选择唯一块或全部出现并统计短块
//输入：input：完整实验输入；mode：unique 或 all。
//输出：返回出现位置数组和所选字节/短块计数；短块仍留在所选列表，提取循环另行跳过，非法模式抛异常。
auto selectSFInput(const ExperimentInput &input, const std::string_view mode) -> SFInputSelection {
    SFInputSelection selected;
    if (mode == "unique") {
        selected.chunkPositions = input.uniqueChunkPositions;
    } else if (mode == "all") {
        selected.chunkPositions.reserve(input.chunks.size());
        for (std::size_t i = 0; i < input.chunks.size(); ++i) {
            selected.chunkPositions.push_back(i);
        }
    } else {
        throw std::invalid_argument("sfInput must be unique or all");
    }
    for (const auto position : selected.chunkPositions) {
        const auto &chunk = input.chunks.at(position);
        selected.inputBytes = checkedAdd(selected.inputBytes, chunk.length);
        if (chunk.length < FEATURE_COUNT) {
            selected.excludedChunks = checkedAdd(selected.excludedChunks, 1);
            selected.excludedBytes = checkedAdd(selected.excludedBytes, chunk.length);
        }
    }
    return selected;
}
