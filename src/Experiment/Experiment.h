#pragma once

#include "Common/Types.h"

#include <filesystem>
#include <map>
#include <string>
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
    std::string bytes;
    std::string digest;
};

struct InputChunk {
    ChunkId id = 0;
    std::size_t fileIndex = 0;
    std::uint64_t offset = 0;
    std::uint64_t length = 0;
};

struct ExperimentInput {
    std::vector<SourceFile> files;
    std::vector<InputChunk> chunks;
    std::uint64_t inputBytes = 0;
    std::uint64_t excludedBytes = 0;
    std::uint64_t excludedChunks = 0;
    std::int64_t cdcElapsedNs = 0;
};

auto loadExperimentConfig(const std::filesystem::path &path) -> ExperimentConfig;
auto loadExperimentInput(const ExperimentConfig &config) -> ExperimentInput;
auto runExperiment(const ExperimentConfig &config) -> void;
