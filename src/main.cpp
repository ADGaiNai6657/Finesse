#include "Experiment/Experiment.h"

#include <exception>
#include <iostream>
#include <string_view>

int main(const int argc, char **argv) {
    if (argc == 2 && std::string_view(argv[1]) == "--help") {
        std::cout << "Usage: Finesse --config <file>\nFinesse-Gear / NTransform-Gear SF microbenchmark (D-17).\n";
        return 0;
    }
    if (argc != 3 || std::string_view(argv[1]) != "--config") {
        std::cerr << "Usage: Finesse --config <file>\n";
        return 2;
    }
    try {
        const auto config = loadExperimentConfig(argv[2]);
        runExperiment(config);
        std::cout << "Completed: " << config.outputDirectory << '\n';
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "Experiment failed: " << error.what() << '\n';
        return 1;
    }
}
