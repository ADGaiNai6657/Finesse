#include "DeltaExperiment.h"

#include "BuildInfo.h"
#include "DeltaCodec/DeltaCodec.h"
#include "EncodedStore/EncodedStore.h"
#include "IO.h"

#include <array>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <sys/utsname.h>

//去掉配置行首尾的空格、制表符和回车
//输入：value：待处理的字符串。
//输出：返回裁剪后的副本，全部为空白时返回空串，不修改原串。
static auto trim(const std::string &value) -> std::string {
    const auto begin = value.find_first_not_of(" \t\r");
    return begin == std::string::npos ? "" : value.substr(begin, value.find_last_not_of(" \t\r") - begin + 1);
}

//读取并严格检查独立 Delta 管线配置
//输入：path：16 键配置的路径，相对 inputConfig 以本文件目录为基准。
//输出：返回配置及引用的输入配置；拒绝未知、重复、缺失、未批准的值或 sfInput!=unique。
auto loadDeltaExperimentConfig(const std::filesystem::path &path) -> DeltaExperimentConfig {
    const std::map<std::string, std::string> frozen {
        {"pipelineProfile", "delta-memory-v1"}, {"algorithms", "Finesse-Gear,NTransform-Gear"},
        {"codec", "xdelta3"}, {"codecRevision", std::string(XDELTA_REVISION)}, {"compressionLevel", "6"},
        {"secondaryCompression", "none"}, {"checksum", "adler32"}, {"basePolicy", "firstfit-raw-only-cross-slot"},
        {"maxDeltaDepth", "1"}, {"gainPolicy", "payload-strictly-smaller"}, {"storage", "memory-raw-delta"},
        {"verification", "byte-equality"}, {"pipelineRuns", "1"}};
    DeltaExperimentConfig config;
    config.configPath = std::filesystem::canonical(path);
    const auto bytes = readFile(config.configPath);
    requireUtf8(bytes);
    std::istringstream input(bytes);
    std::string line;
    while (std::getline(input, line)) {
        line = trim(line);
        if (line.empty() || line.front() == '#') {
            continue;
        }
        const auto separator = line.find('=');
        if (separator == std::string::npos) {
            throw std::invalid_argument("Delta config line has no '='");
        }
        const auto key = trim(line.substr(0, separator));
        const auto value = trim(line.substr(separator + 1));
        if ((!frozen.contains(key) && key != "inputConfig" && key != "outputDirectory" && key != "runId")
            || value.empty() || !config.values.emplace(key, value).second) {
            throw std::invalid_argument("unknown, empty or duplicate Delta config key: " + key);
        }
    }
    if (config.values.size() != frozen.size() + 3) {
        throw std::invalid_argument("missing required Delta config key");
    }
    for (const auto &[key, value] : frozen) {
        if (config.values.at(key) != value) {
            throw std::invalid_argument("Delta config differs from D-22..D-24: " + key);
        }
    }
    for (const unsigned char c : config.values.at("runId")) {
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) {
            throw std::invalid_argument("invalid Delta runId");
        }
    }
    config.outputDirectory = pathFrom(config.configPath.parent_path(), config.values.at("outputDirectory"));
    config.inputConfig = loadExperimentConfig(pathFrom(config.configPath.parent_path(), config.values.at("inputConfig")));
    if (config.inputConfig.values.at("sfInput") != "unique") {
        throw std::invalid_argument("Delta inputConfig requires sfInput=unique");
    }
    return config;
}

//将字节计数的总体比例格式化成文本
//输入：numerator/denominator：分子和分母的 uint64 计数。
//输出：返回 17 位有效数字的比值文本；分母为 0 时返回空串，表示缺失值。
static auto ratio(const std::uint64_t numerator, const std::uint64_t denominator) -> std::string {
    if (denominator == 0) {
        return "";
    }
    std::ostringstream out;
    out << std::setprecision(17) << static_cast<double>(numerator) / static_cast<double>(denominator);
    return out.str();
}

//取得 Delta 运行的当前 UTC 时间
//输入：无，读取系统时钟。
//输出：返回 YYYY-MM-DDTHH:MM:SSZ 格式的时间戳文本。
static auto timestamp() -> std::string {
    const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm utc {};
    gmtime_r(&now, &utc);
    std::ostringstream out;
    out << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
    return out.str();
}

//构造两算法编码池并在释放原文后验证全部出现
//输入：config：已批准的单次 Delta 配置及输入配置。
//输出：无返回值；导出真实编码、候选尝试、恢复证据和质量统计，不执行 SF 五轮；失败记 failed 并抛异常，拒绝覆盖。
auto runDeltaExperiment(const DeltaExperimentConfig &config) -> void {
    const auto &directory = config.outputDirectory;
    std::filesystem::create_directories(directory.parent_path());
    if (!std::filesystem::create_directory(directory)) {
        throw std::runtime_error("result directory already exists: " + directory.string());
    }
    std::string metadata = "timestamp=" + timestamp() + "\napprovals=D-22,D-23,D-24\n";
    try {
        for (const auto &[key, value] : config.values) {
            metadata += key + "=" + value + "\n";
        }
        for (const auto &[key, value] : config.inputConfig.values) {
            metadata += "input." + key + "=" + value + "\n";
        }
        struct utsname platform {};
        if (uname(&platform) != 0) {
            throw std::runtime_error("uname failed");
        }
        metadata += "codeRevision=" + std::string(CODE_REVISION) + "\nsourceDigest=" + SOURCE_DIGEST
            + "\nsourceState=" + SOURCE_STATE + "\ncompiler=" + COMPILER_INFO + "\nbuildType=" + BUILD_TYPE
            + "\nbuildOptions=" + BUILD_OPTIONS + "\nos=" + platform.sysname + " " + platform.release
            + "\narchitecture=" + platform.machine + "\nxdeltaSourceVersion=3.2.2+post-release-fixes\nxdeltaSourceManifestSha256="
            + XDELTA_SOURCE_MANIFEST_SHA256 + "\nxdeltaProfile=" + std::string(XDELTA_PROFILE)
            + "\nxdeltaBuild=C99 static; portable memcpy unaligned reads; no CLI/armor/liblzma/secondary\n"
            "threads=1\nprofileId=" + std::string(PROFILE_ID) + "\ngearTableRevision=" + std::string(UPSTREAM_REVISION)
            + "\ngearTableSha256=" + GEAR_TABLE_SHA256 + "\nparameterSha256=" + PARAMETER_FILE_SHA256
            + "\ninputConfigSha256=" + sha256(readFile(config.inputConfig.configPath))
            + "\ndeltaConfigSha256=" + sha256(readFile(config.configPath))
            + "\ninputDescriptorSha256=" + sha256(readFile(config.inputConfig.inputPath)) + "\n"
            "contentHash=SHA-1\nduplicateConfirmation=length+byte-equality\nrestoredHash=SHA-256\n"
            "sfBenchmarkExecuted=false\ninputConfigOutputIgnored=true\ninputConfigIterationsIgnored=true\n"
            "shortChunkPolicy=Raw; length<12 excluded from SF/index\n"
            "buildTiming=SF+Raw-index+SHA256+encode+roundtrip-byte-verify+payload-allocation; excludes load/CDC/dedup/export\n"
            "restorationSweepTiming=all occurrences decode+integrity+verification-row materialization; no source reads\n"
            "ratioBlankReason=zero denominator\nmetricScope=payload bytes only; excludes metadata/index/allocator/temporary oracle\n";
        writeFile(directory / "run.txt", "status=running\n" + metadata);
        writeFile(directory / "source-files.csv", SOURCE_FILES_CSV);
        writeFile(directory / "xdelta-source-files.csv", XDELTA_SOURCE_FILES_CSV);
        writeFile(directory / "parameters.csv", experimentParameterCsv());
        std::array<EncodedStore, 2> stores;
        std::vector<InputChunk> occurrences;
        ChunkStoreStats inputStats;
        {
            auto input = loadExperimentInput(config.inputConfig);
            auto exportConfig = config.inputConfig;
            exportConfig.outputDirectory = directory;
            exportExperimentInput(exportConfig, input);
            exportChunkStore(exportConfig, input);
            inputStats = input.store.stats;
            metadata += "cdcElapsedNs=" + std::to_string(input.cdcElapsedNs)
                + "\ndedupElapsedNs=" + std::to_string(input.dedupElapsedNs) + "\n";
            stores[0] = buildEncodedStore(input, "Finesse-Gear");
            stores[1] = buildEncodedStore(input, "NTransform-Gear");
            occurrences = std::move(input.chunks);
        } // 原始 ChunkStore 和所有原文缓冲在此析构。之后恢复只访问 stores。
        metadata += "sourcePoolReleasedBeforeRestoration=true\nsourceFilesReadDuringRestoration=false\n"
            "deltaRetainsOriginalPayload=false\n";
        CsvRows encoded {{"algorithm", "storedId", "firstChunkId", "kind", "baseId", "originalBytes", "payloadBytes", "restoredSha256"}};
        CsvRows attempts {{"algorithm", "storedId", "baseId", "querySlot", "historicalSlot", "originalBytes", "deltaBytes", "accepted", "reason", "encodeNs", "verifyNs"}};
        CsvRows refs {{"chunkId", "storedId", "fileIndex", "offset", "length", "isDuplicate"}};
        for (const auto &occurrence : occurrences) {
            refs.push_back({std::to_string(occurrence.id), std::to_string(occurrence.storedId), std::to_string(occurrence.fileIndex),
                std::to_string(occurrence.offset), std::to_string(occurrence.length), occurrence.isDuplicate ? "true" : "false"});
        }
        writeFile(directory / "occurrences.csv", encodeCsv(refs));
        CsvRows verification {{"algorithm", "chunkId", "storedId", "bytes", "restoredSha256", "status"}};
        CsvRows summary {{"algorithm", "uniqueChunks", "uniqueOriginalBytes", "finalPayloadBytes", "rawChunks", "deltaChunks", "matchedAttempts", "rawFallbacks",
            "attemptedOriginalBytes", "attemptedDeltaBytes", "acceptedOriginalBytes", "acceptedDeltaBytes", "dcr", "storedResidualRatio", "storedSavingRatio",
            "attemptedResidualRatio", "attemptedSavingRatio", "acceptedResidualRatio", "acceptedSavingRatio", "maxDeltaDepth", "verifiedOccurrences", "verifiedBytes",
            "buildNs", "encodeNs", "roundtripVerifyNs", "restorationSweepNs", "pipelineRuns"}};
        for (const auto &store : stores) {
            std::uint64_t payload = 0, rawCount = 0, deltaCount = 0;
            std::uint64_t attemptedOriginal = 0, attemptedDelta = 0, acceptedOriginal = 0, acceptedDelta = 0;
            std::uint64_t encodeNs = 0, roundtripNs = 0, verifiedBytes = 0;
            for (const auto &record : store.chunks) {
                payload = checkedAdd(payload, record.payload.size());
                record.kind == PayloadKind::Raw ? ++rawCount : ++deltaCount;
                encoded.push_back({store.algorithm, std::to_string(record.id), std::to_string(record.firstChunkId),
                    record.kind == PayloadKind::Raw ? "Raw" : "Delta", record.baseId ? std::to_string(*record.baseId) : "",
                    std::to_string(record.originalLength), std::to_string(record.payload.size()), record.restoredSha256});
            }
            for (const auto &attempt : store.attempts) {
                attemptedOriginal = checkedAdd(attemptedOriginal, attempt.originalBytes);
                attemptedDelta = checkedAdd(attemptedDelta, attempt.deltaBytes);
                encodeNs = checkedAdd(encodeNs, static_cast<std::uint64_t>(attempt.encodeNs));
                roundtripNs = checkedAdd(roundtripNs, static_cast<std::uint64_t>(attempt.verifyNs));
                if (attempt.accepted) {
                    acceptedOriginal = checkedAdd(acceptedOriginal, attempt.originalBytes);
                    acceptedDelta = checkedAdd(acceptedDelta, attempt.deltaBytes);
                }
                attempts.push_back({store.algorithm, std::to_string(attempt.id), std::to_string(attempt.candidate.chunkId),
                    std::to_string(attempt.candidate.querySlot), std::to_string(attempt.candidate.historicalSlot),
                    std::to_string(attempt.originalBytes), std::to_string(attempt.deltaBytes), attempt.accepted ? "true" : "false",
                    attempt.accepted ? "strictly-smaller" : "no-payload-gain", std::to_string(attempt.encodeNs), std::to_string(attempt.verifyNs)});
            }
            const auto verifyStart = std::chrono::steady_clock::now();
            for (const auto &occurrence : occurrences) {
                const auto bytes = restoreEncodedChunk(store, occurrence.storedId);
                if (bytes.size() != occurrence.length) {
                    throw std::runtime_error("occurrence restored length mismatch");
                }
                verifiedBytes = checkedAdd(verifiedBytes, bytes.size());
                verification.push_back({store.algorithm, std::to_string(occurrence.id), std::to_string(occurrence.storedId),
                    std::to_string(bytes.size()), sha256(bytes), "ok"});
            }
            const auto verificationNs = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - verifyStart).count();
            if (verifiedBytes != inputStats.totalBytes || payload > inputStats.uniqueBytes || store.chunks.size() != inputStats.uniqueChunks) {
                throw std::runtime_error("encoded store totals failed");
            }
            // 尝试可能为负收益，saving 不能在无符号整数上相减。
            const auto attemptedResidual = ratio(attemptedDelta, attemptedOriginal);
            //由剩余比例计算节省比例
            //输入：residual 为剩余比例文本，空串表示比例缺失。
            //输出：返回 1-residual 的文本，缺失仍返回空串，不做无符号减法。
            const auto saving = [](const std::string &residual) -> std::string {
                if (residual.empty()) { return ""; }
                std::ostringstream value;
                value << std::setprecision(17) << 1.0 - std::stod(residual);
                return value.str();
            };
            summary.push_back({store.algorithm, std::to_string(store.chunks.size()), std::to_string(inputStats.uniqueBytes), std::to_string(payload),
                std::to_string(rawCount), std::to_string(deltaCount), std::to_string(store.attempts.size()), std::to_string(store.attempts.size() - deltaCount),
                std::to_string(attemptedOriginal), std::to_string(attemptedDelta), std::to_string(acceptedOriginal), std::to_string(acceptedDelta),
                ratio(inputStats.uniqueBytes, payload), ratio(payload, inputStats.uniqueBytes), ratio(inputStats.uniqueBytes - payload, inputStats.uniqueBytes),
                attemptedResidual, saving(attemptedResidual), ratio(acceptedDelta, acceptedOriginal), ratio(acceptedOriginal - acceptedDelta, acceptedOriginal),
                deltaCount == 0 ? "0" : "1", std::to_string(occurrences.size()), std::to_string(verifiedBytes), std::to_string(store.buildNs),
                std::to_string(encodeNs), std::to_string(roundtripNs), std::to_string(verificationNs), "1"});
        }
        writeFile(directory / "encoded-chunks.csv", encodeCsv(encoded));
        writeFile(directory / "delta-attempts.csv", encodeCsv(attempts));
        writeFile(directory / "restoration-verification.csv", encodeCsv(verification));
        writeFile(directory / "compression-summary.csv", encodeCsv(summary));
        for (const auto name : {"manifest.csv", "checksums.csv", "source-checksums.csv", "encoded-chunks.csv", "delta-attempts.csv", "occurrences.csv", "restoration-verification.csv", "compression-summary.csv"}) {
            metadata += std::string(name) + ".sha256=" + sha256(readFile(directory / name)) + "\n";
        }
        writeFile(directory / "run.txt", "status=ok\nrestorationStatus=ok\n" + metadata);
    } catch (const std::exception &error) {
        std::error_code ignored;
        std::filesystem::remove(directory / "compression-summary.csv", ignored);
        try {
            writeFile(directory / "run.txt", "status=failed\nreason=" + std::string(error.what()) + "\n" + metadata);
        } catch (...) {
            // 保留原异常，不将失败伪装成 Raw 回退。
        }
        throw;
    }
}
