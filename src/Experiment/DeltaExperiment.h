#pragma once

#include "Experiment.h"

struct DeltaExperimentConfig {
    std::filesystem::path configPath;
    std::filesystem::path outputDirectory;
    std::map<std::string, std::string> values;
    ExperimentConfig inputConfig;
};

auto loadDeltaExperimentConfig(const std::filesystem::path &path) -> DeltaExperimentConfig;
auto runDeltaExperiment(const DeltaExperimentConfig &config) -> void;
