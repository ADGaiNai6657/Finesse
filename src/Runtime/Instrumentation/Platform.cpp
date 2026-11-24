#include "Platform.h"

#include <array>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#else
#include <sys/resource.h>
#include <sys/utsname.h>
#include <unistd.h>
#ifdef __APPLE__
#include <sys/sysctl.h>
#endif
#endif

namespace metrics {

// 使用平台提供的线程安全 UTC 转换。该时间仅用于显示和溯源。
auto utcTimestamp() -> std::string {
    const auto now = std::time(nullptr);
    std::tm utc {};
#ifdef _WIN32
    if (gmtime_s(&utc, &now) != 0) return {};
#else
    if (!gmtime_r(&now, &utc)) return {};
#endif
    std::ostringstream text;
    text << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
    return text.str();
}

// 从平台 API 读取进程标识。
auto processId() -> std::uint64_t {
#ifdef _WIN32
    return GetCurrentProcessId();
#else
    return static_cast<std::uint64_t>(getpid());
#endif
}

#ifdef _WIN32
// Windows CPU 计量单位为 100 ns。转换为微秒后与 POSIX 路径统一。
static auto fileTimeUs(const FILETIME &time) -> double {
    const auto ticks = (std::uint64_t{time.dwHighDateTime} << 32) | time.dwLowDateTime;
    return static_cast<double>(ticks) / 10;
}
#endif

// Windows 工作集和 macOS ru_maxrss 已以字节计量，Linux 的 ru_maxrss 需换算。
auto resourceSnapshot() -> ResourceSnapshot {
    ResourceSnapshot snapshot;
#ifdef _WIN32
    FILETIME created {}, exited {}, kernel {}, user {};
    if (GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) {
        snapshot.cpuTimeUs = fileTimeUs(kernel) + fileTimeUs(user);
    }
    PROCESS_MEMORY_COUNTERS memory {};
    memory.cb = sizeof(memory);
    if (GetProcessMemoryInfo(GetCurrentProcess(), &memory, sizeof(memory))) {
        snapshot.peakRssBytes = memory.PeakWorkingSetSize;
    }
#else
    rusage usage {};
    if (getrusage(RUSAGE_SELF, &usage) == 0) {
        snapshot.cpuTimeUs = (usage.ru_utime.tv_sec + usage.ru_stime.tv_sec) * 1e6
            + usage.ru_utime.tv_usec + usage.ru_stime.tv_usec;
#ifdef __APPLE__
        snapshot.peakRssBytes = static_cast<std::uint64_t>(usage.ru_maxrss);
#else
        snapshot.peakRssBytes = static_cast<std::uint64_t>(usage.ru_maxrss) * 1024;
#endif
    }
#endif
    return snapshot;
}

#ifdef _WIN32
// 使用原生架构，避免 x64 兼容进程将 ARM64 主机报告为 x86。
static auto architectureName(const WORD architecture) -> std::string {
    switch (architecture) {
        case PROCESSOR_ARCHITECTURE_AMD64: return "x86_64";
        case PROCESSOR_ARCHITECTURE_ARM64: return "arm64";
        case PROCESSOR_ARCHITECTURE_INTEL: return "x86";
        case PROCESSOR_ARCHITECTURE_ARM: return "arm";
        default: return {};
    }
}
#endif

// 采集 OS 和机器信息。所有可选资源字段只在读取成功时写入。
auto hostInfo() -> HostInfo {
    HostInfo host;
#ifdef _WIN32
    host.os = "Windows";
    // RtlGetVersion 返回系统真实版本，避免应用 manifest 影响版本报告。
    using GetVersion = LONG (WINAPI *)(OSVERSIONINFOW *);
    const auto ntdll = GetModuleHandleW(L"ntdll.dll");
    const auto getVersion = ntdll ? reinterpret_cast<GetVersion>(GetProcAddress(ntdll, "RtlGetVersion")) : nullptr;
    OSVERSIONINFOW version {};
    version.dwOSVersionInfoSize = sizeof(version);
    if (getVersion && getVersion(&version) == 0) {
        host.os += " " + std::to_string(version.dwMajorVersion) + "." + std::to_string(version.dwMinorVersion)
            + "." + std::to_string(version.dwBuildNumber);
    }
    SYSTEM_INFO system {};
    GetNativeSystemInfo(&system);
    host.architecture = architectureName(system.wProcessorArchitecture);
    std::array<char, 256> cpu {};
    DWORD cpuSize = static_cast<DWORD>(cpu.size());
    if (RegGetValueA(HKEY_LOCAL_MACHINE, "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
                     "ProcessorNameString", RRF_RT_REG_SZ, nullptr, cpu.data(), &cpuSize) == ERROR_SUCCESS) {
        host.cpu = cpu.data();
    }
    MEMORYSTATUSEX memory {};
    memory.dwLength = sizeof(memory);
    if (GlobalMemoryStatusEx(&memory)) host.physicalMemory = memory.ullTotalPhys;
#else
    utsname system {};
    if (uname(&system) == 0) {
        host.os = std::string(system.sysname) + " " + system.release;
        host.architecture = system.machine;
    }
#ifdef __APPLE__
    std::array<char, 256> cpu {};
    auto size = cpu.size();
    if (sysctlbyname("machdep.cpu.brand_string", cpu.data(), &size, nullptr, 0) == 0) host.cpu = cpu.data();
    std::uint64_t memory = 0;
    size = sizeof(memory);
    if (sysctlbyname("hw.memsize", &memory, &size, nullptr, 0) == 0) host.physicalMemory = memory;
#else
    const auto pages = sysconf(_SC_PHYS_PAGES);
    const auto pageSize = sysconf(_SC_PAGESIZE);
    if (pages > 0 && pageSize > 0) host.physicalMemory = std::uint64_t(pages) * std::uint64_t(pageSize);
    std::ifstream cpuInfo("/proc/cpuinfo");
    std::string line;
    while (std::getline(cpuInfo, line)) {
        if (!line.starts_with("model name") && !line.starts_with("Hardware")) continue;
        const auto separator = line.find(':');
        if (separator == std::string::npos) continue;
        const auto first = line.find_first_not_of(" \t", separator + 1);
        if (first != std::string::npos) host.cpu = line.substr(first);
        break;
    }
#endif
#endif
    return host;
}

} // namespace metrics
