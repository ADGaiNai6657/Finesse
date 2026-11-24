#include "Metrics.h"

#include <algorithm>
#include <exception>
#include <stdexcept>

namespace metrics {
static thread_local Collector *current = nullptr;
static thread_local Context context;
static constexpr std::size_t SampleLimit = 4096;

// 解析 CLI 模式。未知标记直接报错，避免误采样。
auto parseMode(const std::string_view value) -> Mode {
    if (value == "off") return Mode::Off;
    if (value == "benchmark") return Mode::Benchmark;
    if (value == "profiling") return Mode::Profiling;
    throw std::invalid_argument("instrumentation mode must be off, benchmark or profiling");
}

// 聚合一次操作的耗时、数据量与失败次数。
static void accumulate(Timing &timing, const Operation &op) {
    timing.min = timing.count == 0 ? op.elapsed : std::min(timing.min, op.elapsed);
    timing.max = std::max(timing.max, op.elapsed);
    ++timing.count;
    timing.failures += op.result == "failed";
    timing.total += op.elapsed;
    // 没有字节口径的操作保持未知，真实的空输入仍可以记录为 0。
    if (op.inputBytes != NoId) {
        timing.inputBytes += op.inputBytes;
        timing.hasInputBytes = true;
    }
    if (op.outputBytes != NoId) {
        timing.outputBytes += op.outputBytes;
        timing.hasOutputBytes = true;
    }
}

// 写入线程内阶段和文件聚合。仅 Profiling 缓存选中的明细。
void Collector::record(const Operation op, const bool detail) {
    auto &timing = stages[{op.context.algorithm, op.context.phase, op.stage}];
    accumulate(timing, op);
    // 限制每个阶段保留的样本。其余调用仍累加计数、字节与耗时。
    if (mode == Mode::Profiling && timing.samples.size() < SampleLimit) {
        timing.samples.push_back(op.elapsed);
    }
    if (op.context.file != NoId) {
        accumulate(files[{op.context.file, op.context.algorithm, op.context.phase, op.stage}], op);
    }
    if (mode == Mode::Profiling && detail) {
        operations.push_back(op);
    }
}

// 在当前算法和阶段累加指定的计数器。
void Collector::count(const std::string_view name, const std::uint64_t increment) {
    counters[{context.algorithm, context.phase, name}] += increment;
}

// 合并工作线程的调用总数、极值、累计时长及有界样本。
void Timing::merge(const Timing &other) {
    if (other.count == 0) return;
    min = count == 0 ? other.min : std::min(min, other.min);
    max = std::max(max, other.max);
    count += other.count;
    failures += other.failures;
    total += other.total;
    inputBytes += other.inputBytes;
    outputBytes += other.outputBytes;
    hasInputBytes = hasInputBytes || other.hasInputBytes;
    hasOutputBytes = hasOutputBytes || other.hasOutputBytes;
    const auto size = std::min(SampleLimit - samples.size(), other.samples.size());
    samples.insert(samples.end(), other.samples.begin(), other.samples.begin() + size);
}

// 合并已结束的线程数据。明细 ID 由导出端统一分配。
void Collector::merge(const Collector &other) {
    if (mode != other.mode) throw std::invalid_argument("cannot merge different instrumentation modes");
    for (const auto &[key, value] : other.stages) stages[key].merge(value);
    for (const auto &[key, value] : other.files) files[key].merge(value);
    for (const auto &[key, value] : other.counters) counters[key] += value;
    operations.insert(operations.end(), other.operations.begin(), other.operations.end());
    for (const auto &[key, definition] : other.definitions) {
        const auto [entry, inserted] = definitions.try_emplace(key, definition);
        if (!inserted && entry->second != definition) throw std::invalid_argument("metric definition conflict");
    }
    complete = complete && other.complete;
}

// 替换当前线程的活动采集器并重置上下文。
Activate::Activate(Collector &collector) : previous(current), previousContext(context) {
    current = collector.mode == Mode::Off ? nullptr : &collector;
    context = {};
}

// 恢复嵌套运行之前的线程采集器和标签。
Activate::~Activate() {
    current = previous;
    context = previousContext;
}

// 设置算法和阶段标签，并保存外层作用域。
Scope::Scope(const std::string_view algorithm, const std::string_view phase) : previous(context) {
    context.algorithm = algorithm;
    context.phase = phase;
}

// 补充文件和块 ID，沿用外层算法和阶段。
Scope::Scope(const std::uint64_t file, const std::uint64_t chunk) : previous(context) {
    context.file = file;
    context.chunk = chunk;
}

// 离开操作后恢复旧标签，避免上下文串到下一块。
Scope::~Scope() { context = previous; }

// 只有启用统一采集或需要旧纳秒值时才读单调时钟。
Timer::Timer(const std::string_view stage, const std::uint64_t inputBytes, const bool detail,
             std::int64_t *legacyNs)
    : collector(current), context(metrics::context), stage(stage), inputBytes(inputBytes), detail(detail),
      legacyNs(legacyNs), exceptions(collector ? std::uncaught_exceptions() : 0),
      start(collector || legacyNs ? Clock::now() : Clock::time_point {}) {}

// 异常展开时自动标记失败，已结束的计时器直接返回。
Timer::~Timer() {
    finish(collector && std::uncaught_exceptions() > exceptions ? "failed" : "ok");
}

// 停止计时，同时写出微秒统计和可选的旧纳秒累计。
void Timer::finish(const std::string_view result, const std::uint64_t outputBytes,
                          const std::uint64_t relatedChunk) noexcept {
    if (!collector && !legacyNs) return;
    // 计时先停止，后做聚合。嵌套的外层阶段仍自然包含内层采集开销。
    const auto elapsed = Clock::now() - start;
    const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count();
    if (legacyNs) *legacyNs += ns;
    if (collector) {
        try {
            collector->record({context, stage, result, Microseconds(elapsed).count(), inputBytes, outputBytes, relatedChunk}, detail);
        } catch (...) {
            // 统计分配失败只降低结果完备性，不能在异常展开时再抛异常。
            collector->complete = false;
        }
    }
    collector = nullptr;
    legacyNs = nullptr;
}

// 登记算法专用计数器的含义和单位，同名定义必须一致。
void registerCounter(const std::string_view name, const CounterDefinition definition) {
    if (!current) return;
    const auto [entry, inserted] = current->definitions.try_emplace(std::make_pair(context.algorithm, name), definition);
    if (!inserted && entry->second != definition) throw std::invalid_argument("metric definition conflict");
}

// 将计数增量发给本线程的采集器。
void count(const std::string_view name, const std::uint64_t increment) {
    if (current) current->count(name, increment);
}

// 检查当前线程是否开启统一统计。
auto active() -> bool { return current != nullptr; }
// 判断当前线程是否要采集高频明细。
auto profiling() -> bool { return current && current->mode == Mode::Profiling; }
} // namespace metrics
