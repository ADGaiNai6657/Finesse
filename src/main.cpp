#include "Experiment/Experiment.h"
#include "Experiment/DeltaExperiment.h"

#include <exception>
#include <iostream>
#include <string_view>

//解析命令行并调用 SF 或 Delta 实验入口
//输入：argc/argv：程序参数，支持 --help、--config 和 --delta-config。
//输出：返回退出码：成功为 0，参数用法错误为 2，实验异常为 1；输出运行结果或错误提示。
int main(const int argc, char **argv) {
    if (argc == 2 && std::string_view(argv[1]) == "--help") {
        std::cout << "Usage: Finesse --config <file> | --delta-config <file>\nFinesse-Gear / NTransform-Gear: SF microbenchmark or recoverable in-memory Delta pipeline.\n";
        return 0;
    }
    if (argc != 3 || (std::string_view(argv[1]) != "--config" && std::string_view(argv[1]) != "--delta-config")) {
        std::cerr << "Usage: Finesse --config <file> | --delta-config <file>\n";
        return 2;
    }
    try {
        if (std::string_view(argv[1]) == "--delta-config") {
            const auto config = loadDeltaExperimentConfig(argv[2]);
            runDeltaExperiment(config);
            std::cout << "Completed recoverable Delta pipeline: " << config.outputDirectory << '\n';
            return 0;
        }
        const auto config = loadExperimentConfig(argv[2]);
        runExperiment(config);
        std::cout << "Completed: " << config.outputDirectory << '\n';
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "Experiment failed: " << error.what() << '\n';
        return 1;
    }
}
