#include "Instrumentation/Metrics.h"

#include <array>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <thread>

// 验证实际工作线程的局部统计、异常计时和合并一致性。
int main() {
    // 每个线程实际记录自己的阶段及明细，主线程在 join 后合并。
    std::array workers {metrics::Collector(metrics::Mode::Profiling), metrics::Collector(metrics::Mode::Profiling)};
    const auto work = [](metrics::Collector &collector) {
        const metrics::Activate current(collector);
        const metrics::Scope experiment("TestAlgorithm", "sample");
        const metrics::Scope file(0, 0);
        metrics::registerCounter("records", {"count", "test records"});
        metrics::count("records");
        try {
            const metrics::Timer timer("process", 4, true);
            throw std::runtime_error("operation failed");
        } catch (const std::runtime_error &) {}
        metrics::Timer timer("process", 4, true);
        timer.finish("ok", 2);
    };
    std::jthread a(work, std::ref(workers[0]));
    std::jthread b(work, std::ref(workers[1]));
    a.join();
    b.join();
    metrics::Collector merged(metrics::Mode::Profiling);
    merged.merge(workers[0]);
    merged.merge(workers[1]);
    const auto &timing = merged.stages.at({"TestAlgorithm", "sample", "process"});
    if (timing.count != 4 || timing.failures != 2 || timing.inputBytes != 16 || timing.outputBytes != 4
        || timing.samples.size() != 4 || merged.operations.size() != 4
        || merged.counters.at({"TestAlgorithm", "sample", "records"}) != 2
        || merged.files.at({0, "TestAlgorithm", "sample", "process"}).count != 4) {
        std::cerr << "merged instrumentation statistics do not reconcile\n";
        return 1;
    }
    return 0;
}
