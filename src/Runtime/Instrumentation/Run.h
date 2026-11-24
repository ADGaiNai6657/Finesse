#pragma once

#include "Metrics.h"

#include <optional>

struct ExperimentInput;
struct EncodedStore;
struct SourceFile;

namespace metrics {

// 管理单次 CLI 运行的输出、资源基线和实体间的关联。
class Run {
public:
    // 启动采集并保存实验配置快照。
    Run(Mode mode, std::filesystem::path output, std::string_view kind,
        const std::map<std::string, std::string> &config,
        const std::map<std::string, std::string> &inputConfig = {});
    // 保存文件及块元信息，原文字节由既有输入池维护。
    void captureInput(const ExperimentInput &input);
    // 在所有模式下输出完整文件校验证据。
    auto exportRecoveryEvidence() const -> bool;
    // 记录一个文件的恢复状态和摘要。
    void recordRecovery(std::string_view algorithm, std::size_t file, std::uint64_t bytes,
                        std::string_view expected, std::string_view actual, bool ok);
    // 从一个实际编码池提取字节统计。
    void captureStorage(const EncodedStore &store);
    // 写出统一统计汇总及完成状态。
    void finish(bool success, std::string_view reason = {});

private:
    // 导出阶段、文件、块与存储表。
    void exportTimings();
    // 导出筛选过的逐操作明细。
    void exportOperations() const;
    // 导出算法扩展指标的 JSONL。
    void exportAlgorithmMetrics() const;
    // 导出索引操作计数和耗时。
    void exportIndices() const;
    // 列出本次实际写出的统一产物。
    auto exportedFiles() const -> std::string;

    Clock::time_point start = Clock::now();
    std::string started;
    Collector collector;
    Activate activation;
    std::filesystem::path directory;
    std::string id, kind;
    std::uint64_t inputBytes = 0, sourceBytes = 0, chunks = 0, unique = 0, fileCount = 0;
    std::vector<std::vector<std::string>> fileRows, chunkRows, storageRows, recoveryRows;
    std::vector<std::vector<std::string>> fileStorageRows, chunkStorageRows;
    std::vector<std::size_t> firstFileByStoredId;
    std::optional<double> cpuStart;
};

} // namespace metrics
