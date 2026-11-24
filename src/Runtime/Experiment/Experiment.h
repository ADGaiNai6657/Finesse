#pragma once

#include "Common/Types.h"
#include "Instrumentation/Metrics.h"
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

//读取并严格检查 SF 实验配置
//输入：path：配置文件路径，文件内相对路径以配置目录为基准。
//输出：返回配置值、解析后的输入和输出路径及轮数；未知、重复、缺失或未批准的配置抛异常。
auto loadExperimentConfig(const std::filesystem::path &path) -> ExperimentConfig;
//按 raw 或 manifest 加载、校验并精确去重输入
//输入：config：已检查的输入模式、路径和 CDC 配置。
//输出：返回来源元信息、全部出现、唯一原文池和计数；源缓冲在返回前释放，I/O、范围、摘要或计数错误抛异常。
auto loadExperimentInput(const ExperimentConfig &config) -> ExperimentInput;
//为 SF 微基准选择唯一块或全部出现并统计短块
//输入：input：完整实验输入；mode：unique 或 all。
//输出：返回出现位置数组和所选字节/短块计数；短块仍留在所选列表，提取循环另行跳过，非法模式抛异常。
auto selectSFInput(const ExperimentInput &input, std::string_view mode) -> SFInputSelection;
//从出现记录定位池内唯一原文
//输入：input：拥有原文池的实验输入；chunk：带 storedId 和长度的出现记录。
//输出：返回借用池内 payload 的 ChunkView；非法 ID 或长度不一致抛异常，input 清空或销毁后失效。
auto inputChunkView(const ExperimentInput &input, const InputChunk &chunk) -> ChunkView;
//执行 SF 微基准、校验输出并导出统计
//输入：config：已校验的实验配置和新结果目录。
//输出：无返回值；加载去重后运行一次预热、五轮交替测量，导出摘要/汇总/可选诊断；失败记录原因并抛异常，拒绝覆盖。
auto runExperiment(const ExperimentConfig &config, metrics::Mode mode = metrics::Mode::Benchmark) -> void;

// 两种实验入口共用同一来源/清单与唯一池统计导出契约。
//导出全部出现清单和源文件完整性记录
//输入：config：输出目录；input：来源元信息和全部出现。
//输出：无返回值；写 manifest.csv、checksums.csv、source-checksums.csv，包含空源文件摘要，I/O 失败抛异常。
auto exportExperimentInput(const ExperimentConfig &config, const ExperimentInput &input) -> void;
//导出唯一原文、出现引用和精确去重统计
//输入：config：输出目录；input：原文池、全部出现和首次位置映射。
//输出：无返回值；写 stored-chunks.csv、chunk-references.csv、deduplication.csv，不把 payload 写入 CSV，错误抛异常。
auto exportChunkStore(const ExperimentConfig &config, const ExperimentInput &input) -> void;
//导出实际使用的 N-transform 固定参数并校验构建摘要
//输入：无，从 approvedTransformParameters 取得 D-15 参数。
//输出：返回参数 CSV；与构建时参数文件的 SHA-256 不一致时抛异常。
auto experimentParameterCsv() -> std::string;
