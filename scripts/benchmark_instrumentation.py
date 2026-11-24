#!/usr/bin/env python3
"""Compare the three instrumentation modes with the same inputs and checks."""
import argparse
import csv
import os
from pathlib import Path
import statistics
import subprocess
import sys
import time


MODES = ("off", "benchmark", "profiling")


def config_copy(source, destination, output, overrides):
    """复制配置，并修正移动配置后会改变含义的相对输入路径。"""
    values = {}
    for line in source.read_text().splitlines():
        if line.strip() and not line.lstrip().startswith("#"):
            key, value = line.split("=", 1)
            values[key.strip()] = value.strip()
    if "inputPath" in values:
        values["inputPath"] = str((source.parent / values["inputPath"]).resolve())
    values["outputDirectory"] = str(output)
    values.update(overrides)
    destination.write_text("".join(f"{key}={value}\n" for key, value in values.items()))
    return values


def wait_windows(child):
    """Use the Windows process handle for exited-child CPU and peak working set."""
    import ctypes
    from ctypes import wintypes

    class ProcessMemoryCounters(ctypes.Structure):
        """Layout of the Windows PROCESS_MEMORY_COUNTERS resource record."""
        _fields_ = [("cb", wintypes.DWORD), ("PageFaultCount", wintypes.DWORD)] + [
            (name, ctypes.c_size_t) for name in ("PeakWorkingSetSize", "WorkingSetSize",
                "QuotaPeakPagedPoolUsage", "QuotaPagedPoolUsage", "QuotaPeakNonPagedPoolUsage",
                "QuotaNonPagedPoolUsage", "PagefileUsage", "PeakPagefileUsage")
        ]

    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    psapi = ctypes.WinDLL("psapi", use_last_error=True)
    kernel.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
    kernel.OpenProcess.restype = wintypes.HANDLE
    kernel.CloseHandle.argtypes = [wintypes.HANDLE]
    kernel.CloseHandle.restype = wintypes.BOOL
    kernel.GetProcessTimes.argtypes = [wintypes.HANDLE] + [ctypes.POINTER(wintypes.FILETIME)] * 4
    kernel.GetProcessTimes.restype = wintypes.BOOL
    psapi.GetProcessMemoryInfo.argtypes = [
        wintypes.HANDLE, ctypes.POINTER(ProcessMemoryCounters), wintypes.DWORD]
    psapi.GetProcessMemoryInfo.restype = wintypes.BOOL
    handle = kernel.OpenProcess(0x0400 | 0x0010, False, child.pid)
    cpu, rss = None, None
    try:
        child.wait()
        if handle:
            times = [wintypes.FILETIME() for _ in range(4)]
            if kernel.GetProcessTimes(handle, *(ctypes.byref(value) for value in times)):
                cpu = sum((value.dwHighDateTime << 32) | value.dwLowDateTime
                          for value in times[2:]) / 10
            memory = ProcessMemoryCounters()
            memory.cb = ctypes.sizeof(memory)
            if psapi.GetProcessMemoryInfo(handle, ctypes.byref(memory), memory.cb):
                rss = memory.PeakWorkingSetSize
    finally:
        if handle:
            kernel.CloseHandle(handle)
    return cpu, rss


def run(binary, flag, config, mode):
    """测量一个 CLI 子进程从启动到退出的时间、CPU 和 RSS。"""
    start = time.perf_counter_ns()
    with subprocess.Popen([binary, flag, config, "--instrumentation", mode],
                          stdout=subprocess.DEVNULL) as child:
        if sys.platform == "win32":
            cpu, rss = wait_windows(child)
        else:
            _, status, usage = os.wait4(child.pid, 0)
            child.returncode = os.waitstatus_to_exitcode(status)
            cpu = (usage.ru_utime + usage.ru_stime) * 1e6
            rss = usage.ru_maxrss if sys.platform == "darwin" else usage.ru_maxrss * 1024
        if child.returncode:
            raise subprocess.CalledProcessError(child.returncode, child.args)
    elapsed = (time.perf_counter_ns() - start) / 1000
    return elapsed, cpu, rss


def comparable_outputs(directory, delta):
    """提取算法结果，忽略随运行波动的计时字段。"""
    files = ["manifest.csv", "stored-chunks.csv", "chunk-references.csv"]
    if delta:
        files += ["encoded-chunks.csv", "restoration-verification.csv", "file-restoration.csv"]
        dropped = {"encodeNs", "verifyNs", "buildNs", "roundtripVerifyNs", "restorationSweepNs"}
        files += ["delta-attempts.csv", "compression-summary.csv"]
    else:
        files += ["measurements.csv"]
        dropped = {"elapsedNs"}
    result = {}
    for name in files:
        if name == "file-restoration.csv" and not (directory / name).exists():
            continue
        with (directory / name).open(newline="") as stream:
            result[name] = [{key: value for key, value in row.items() if key not in dropped}
                            for row in csv.DictReader(stream)]
    return result


def mean_or_none(values):
    """只对系统实际返回的资源值求平均。"""
    observed = [value for value in values if value is not None]
    return statistics.mean(observed) if observed else None


def main():
    """轮换三种采集模式，逐次核对算法结果并输出开销。"""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("config", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--delta", action="store_true")
    parser.add_argument("--repetitions", type=int, default=3)
    args = parser.parse_args()
    if args.repetitions < 1:
        parser.error("repetitions must be positive")
    source = args.config.resolve()
    root = args.output.resolve()
    root.mkdir(parents=True)
    measurements = []
    reference = None
    # 第 0 轮用于预热；正式轮换顺序，减轻启动和缓存偏差。
    for repetition in range(args.repetitions + 1):
        offset = repetition % len(MODES)
        for mode in MODES[offset:] + MODES[:offset]:
            output = root / f"run-{repetition}-{mode}"
            config = output.with_suffix(".conf")
            values = config_copy(source, config, output, {})
            flag = "--config"
            if args.delta:
                flag = "--delta-config"
                data_config = (source.parent / values["inputConfig"]).resolve()
                inner = output.with_suffix(".input.conf")
                config_copy(data_config, inner, output, {})
                config_copy(source, config, output, {"inputConfig": str(inner)})
            wall, cpu, peak = run(args.binary.resolve(), flag, config, mode)
            current = comparable_outputs(output, args.delta)
            if reference is None:
                reference = current
            elif current != reference:
                raise ValueError(f"algorithm output differs in {output}")
            output_bytes = sum(path.stat().st_size for path in output.iterdir() if path.is_file())
            measurements.append([repetition, mode, wall, cpu, cpu / wall * 100 if cpu is not None else None, peak, output_bytes])
    with (root / "overhead-rounds.csv").open("x", newline="") as stream:
        writer = csv.writer(stream)
        writer.writerow(["repetition", "mode", "wall_time_us", "cpu_time_us", "cpu_percent_one_core",
                         "peak_rss_bytes", "output_bytes"])
        writer.writerows(measurements)
    means = {mode: statistics.mean(r[2] for r in measurements if r[0] and r[1] == mode) for mode in MODES}
    with (root / "overhead-summary.csv").open("x", newline="") as stream:
        writer = csv.writer(stream)
        writer.writerow(["mode", "mean_wall_time_us", "relative_overhead", "mean_cpu_time_us",
                         "max_peak_rss_bytes", "mean_output_bytes", "algorithm_output_equal"])
        for mode in MODES:
            rows = [r for r in measurements if r[0] and r[1] == mode]
            writer.writerow([mode, means[mode], (means[mode] - means["off"]) / means["off"],
                             mean_or_none(r[3] for r in rows), max((r[5] for r in rows if r[5] is not None), default=None),
                             statistics.mean(r[6] for r in rows), "true"])


if __name__ == "__main__":
    main()
