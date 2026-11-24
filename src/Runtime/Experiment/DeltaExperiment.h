#pragma once

#include "Experiment.h"

struct DeltaExperimentConfig {
    std::filesystem::path configPath;
    std::filesystem::path outputDirectory;
    std::map<std::string, std::string> values;
    ExperimentConfig inputConfig;
};

//读取并严格检查独立 Delta 管线配置
//输入：path：16 键配置的路径，相对 inputConfig 以本文件目录为基准。
//输出：返回配置及引用的输入配置；拒绝未知、重复、缺失、未批准的值或 sfInput!=unique。
auto loadDeltaExperimentConfig(const std::filesystem::path &path) -> DeltaExperimentConfig;
//构造两算法编码池并在释放原文后验证全部出现
//输入：config：已批准的单次 Delta 配置及输入配置。
//输出：无返回值；导出真实编码、候选尝试、恢复证据和质量统计，不执行 SF 五轮；失败记 failed 并抛异常，拒绝覆盖。
auto runDeltaExperiment(const DeltaExperimentConfig &config, metrics::Mode mode = metrics::Mode::Benchmark) -> void;
