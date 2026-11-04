#pragma once

#include "Common/Types.h"
#include "ChunkStore/ChunkStore.h"

#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

struct ExperimentConfig {
    std::filesystem::path configPath;
    std::filesystem::path inputPath;
    std::filesystem::path outputDirectory;
    std::map<std::string, std::string> values;
    std::uint64_t iterations = 0;
    bool diagnostics = false;
};

struct SourceFile {
    std::filesystem::path path;
    std::uint64_t length = 0;
    std::string digest;
};

struct InputChunk {
    ChunkId id = 0;
    std::size_t fileIndex = 0;
    std::uint64_t offset = 0;
    std::uint64_t length = 0;
    StoredChunkId storedId = 0;
    bool isDuplicate = false;
};

struct ExperimentInput {
    std::vector<SourceFile> files;
    std::vector<InputChunk> chunks;
    ChunkStore store = makeChunkStore(sha1);
    std::vector<std::size_t> uniqueChunkPositions;
    std::uint64_t inputBytes = 0;
    std::uint64_t excludedBytes = 0;
    std::uint64_t excludedChunks = 0;
    std::int64_t cdcElapsedNs = 0;
    std::int64_t dedupElapsedNs = 0;
};

struct SFInputSelection {
    std::vector<std::size_t> chunkPositions;
    std::uint64_t inputBytes = 0;
    std::uint64_t excludedBytes = 0;
    std::uint64_t excludedChunks = 0;
};

auto loadExperimentConfig(const std::filesystem::path &path) -> ExperimentConfig;
auto loadExperimentInput(const ExperimentConfig &config) -> ExperimentInput;
auto selectSFInput(const ExperimentInput &input, std::string_view mode) -> SFInputSelection;
auto inputChunkView(const ExperimentInput &input, const InputChunk &chunk) -> ChunkView;
auto runExperiment(const ExperimentConfig &config) -> void;

// 两种实验入口共用同一来源/清单与唯一池统计导出契约。
auto exportExperimentInput(const ExperimentConfig &config, const ExperimentInput &input) -> void;
auto exportChunkStore(const ExperimentConfig &config, const ExperimentInput &input) -> void;
auto experimentParameterCsv() -> std::string;
