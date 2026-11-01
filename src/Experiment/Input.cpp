#include "Experiment.h"

#include "BreakingApart/Chunker.h"
#include "IO.h"

#include <algorithm>
#include <chrono>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>

static auto trim(const std::string &value) -> std::string {
    const auto begin = value.find_first_not_of(" \t\r");
    return begin == std::string::npos ? "" : value.substr(begin, value.find_last_not_of(" \t\r") - begin + 1);
}

auto loadExperimentConfig(const std::filesystem::path &path) -> ExperimentConfig {
    const std::set<std::string> keys {"profileId", "inputMode", "inputPath", "datasetKind", "datasetDescription",
        "outputDirectory", "runId", "expectedSize", "minSize", "maxSize", "ncLevel", "warmupRuns",
        "measurementRuns", "iterations", "diagnostics"};
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

static auto loadSource(ExperimentInput &input, const std::filesystem::path &path,
                       std::map<std::filesystem::path, std::size_t> &indices) -> std::size_t {
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
    input.files.push_back({canonical, std::move(bytes), digest});
    indices.emplace(canonical, index);
    return index;
}

auto loadExperimentInput(const ExperimentConfig &config) -> ExperimentInput {
    ExperimentInput input;
    std::map<std::filesystem::path, std::size_t> indices;
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
            const auto index = loadSource(input, path, indices);
            const auto start = std::chrono::steady_clock::now();
            const auto cuts = chunkBoundaries(input.files[index].bytes, cdc);
            input.cdcElapsedNs += std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - start).count();
            std::size_t previous = 0;
            for (const auto cut : cuts) {
                input.chunks.push_back({static_cast<ChunkId>(input.chunks.size()), index, previous, cut - previous});
                previous = cut;
            }
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
            const auto index = loadSource(input, pathFrom(config.inputPath.parent_path(), row[1]), indices);
            const auto offset = parseUnsigned(row[2]);
            const auto length = parseUnsigned(row[3]);
            if (checkedAdd(offset, length) > input.files[index].bytes.size()) {
                throw std::invalid_argument("manifest chunk exceeds source file");
            }
            input.chunks.push_back({id, index, offset, length});
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
    return input;
}
