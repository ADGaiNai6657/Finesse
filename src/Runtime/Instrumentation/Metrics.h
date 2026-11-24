#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <map>
#include <string_view>
#include <tuple>
#include <vector>

namespace metrics {

// 统一采集的三个运行模式。
enum class Mode { Off, Benchmark, Profiling };
// 把命令行字符串转成采集模式。
auto parseMode(std::string_view value) -> Mode;

using Clock = std::chrono::steady_clock;
using Microseconds = std::chrono::duration<double, std::micro>;
inline constexpr auto NoId = UINT64_MAX;

// 名称使用静态字符串或由调用者持有至导出结束。ID 总能关联原始出现记录。
struct Context {
    std::string_view algorithm;
    std::string_view phase;
    std::uint64_t file = NoId;
    std::uint64_t chunk = NoId;
};

// 累计计数与耗时覆盖每次调用。分位数的样本单独保存。
struct Timing {
    std::uint64_t count = 0, failures = 0, inputBytes = 0, outputBytes = 0;
    bool hasInputBytes = false, hasOutputBytes = false;
    double total = 0, min = 0, max = 0;
    std::vector<double> samples;
    // 把另一工作线程的同类阶段结果加到当前记录。
    void merge(const Timing &other);
};

// 注册时声明语义。不同算法可复用名称，同一算法不得重定义。
struct CounterDefinition {
    std::string_view unit = "count";
    std::string_view description;
    bool operator==(const CounterDefinition &) const = default;
};

// 一条结束后的操作记录。标签外借，数据仅保存 ID 和计量值。
struct Operation {
    Context context;
    std::string_view stage, result;
    double elapsed;
    std::uint64_t inputBytes, outputBytes, relatedChunk;
};

// 每个线程使用独立 Collector。计时热路径无需锁或文件 I/O。
class Collector {
public:
    // 创建指定模式的线程本地聚合器。
    explicit Collector(Mode mode) : mode(mode) {}
    Mode mode;
    using Stage = std::tuple<std::string_view, std::string_view, std::string_view>;
    using FileStage = std::tuple<std::uint64_t, std::string_view, std::string_view, std::string_view>;
    std::map<Stage, Timing> stages;
    std::map<FileStage, Timing> files;
    std::map<Stage, std::uint64_t> counters;
    std::map<std::pair<std::string_view, std::string_view>, CounterDefinition> definitions;
    std::vector<Operation> operations;
    bool complete = true;

    // 累加单次操作，按模式决定是否保留明细。
    void record(Operation operation, bool detail);
    // 按当前上下文累加计数。
    void count(std::string_view name, std::uint64_t increment = 1);
    // 每工作线程独立收集，退出后在主线程合并。
    void merge(const Collector &other);
};

// 在当前线程绑定采集器。离开作用域后恢复外层实验上下文。
class Activate {
public:
    // 绑定采集器并保存旧的线程上下文。
    explicit Activate(Collector &collector);
    // 还原先前线程上下文。
    ~Activate();
    Activate(const Activate &) = delete;
    void operator=(const Activate &) = delete;
private:
    Collector *previous;
    Context previousContext;
};

// 标识算法阶段或文件、块。构造数值作用域时继承算法和阶段。
class Scope {
public:
    // 设置算法和阶段标签。
    Scope(std::string_view algorithm, std::string_view phase);
    // 设置当前文件与块标签。
    Scope(std::uint64_t file, std::uint64_t chunk = NoId);
    // 退出时恢复外层标签。
    ~Scope();
    Scope(const Scope &) = delete;
    void operator=(const Scope &) = delete;
private:
    Context previous;
};

// 作用域结束自动记录时长与异常标记。旧字段可从同一时间区间取纳秒。
class Timer {
public:
    // 创建计时器，可指定输入字节、明细及旧纳秒字段。
    explicit Timer(std::string_view stage, std::uint64_t inputBytes = NoId, bool detail = false,
                   std::int64_t *legacyNs = nullptr);
    // 自动结束尚未手动结束的计时。
    ~Timer();
    Timer(const Timer &) = delete;
    void operator=(const Timer &) = delete;
    // 显式停止计时，可附上操作结果、输出大小和候选 ID。
    void finish(std::string_view result = "ok", std::uint64_t outputBytes = NoId,
                        std::uint64_t relatedChunk = NoId) noexcept;
private:
    Collector *collector;
    Context context;
    std::string_view stage;
    std::uint64_t inputBytes;
    bool detail;
    std::int64_t *legacyNs;
    int exceptions;
    Clock::time_point start;
};

// 新算法在进入自己的 Scope 后，可注册扩展计数器和语义。
void registerCounter(std::string_view name, CounterDefinition definition);
// 按当前上下文累加计数。
void count(std::string_view name, std::uint64_t increment = 1);
// 当前线程是否开启统一采集。
auto active() -> bool;
// 当前线程是否开启明细模式。
auto profiling() -> bool;

} // namespace metrics
