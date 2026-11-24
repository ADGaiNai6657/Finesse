#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace metrics {

// 运行机器的环境描述。系统未提供的字段保持为空。
struct HostInfo {
    std::string os;
    std::string architecture;
    std::string cpu;
    std::optional<std::uint64_t> physicalMemory;
};

// 当前进程的累计 CPU 时间和至今峰值常驻内存。
struct ResourceSnapshot {
    std::optional<double> cpuTimeUs;
    std::optional<std::uint64_t> peakRssBytes;
};

// 取得系统版本、架构、CPU 型号及物理内存。
auto hostInfo() -> HostInfo;

// 获取进程级实际观测。资源 API 不可用时返回空值。
auto resourceSnapshot() -> ResourceSnapshot;

// 返回当前进程 ID，用于区分相同 runId 的多个进程。
auto processId() -> std::uint64_t;

// 输出可用于实验溯源的 UTC 时间戳。
auto utcTimestamp() -> std::string;

} // namespace metrics
