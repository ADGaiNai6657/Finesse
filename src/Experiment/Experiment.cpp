#include "Experiment.h"

#include "BreakingApart/BreakingApart.h"
#include "BuildInfo.h"
#include "Finesse/Finesse.h"
#include "IO.h"
#include "NTransform/NTransform.h"
#include "Pipeline/Pipeline.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <ctime>
#include <iomanip>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>
#include <sys/utsname.h>
#include <unistd.h>

#ifdef __APPLE__
#include <mach/mach.h>
#include <sys/sysctl.h>
#endif

static constexpr std::size_t MEASUREMENT_RUNS = 5;
static constexpr std::array<std::string_view, 2> ALGORITHMS {"Finesse-Gear", "NTransform-Gear"};

static auto number(const double value) -> std::string {
    std::ostringstream out;
    out << std::setprecision(17) << value;
    return out.str();
}

static auto utcTimestamp() -> std::string {
    const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm utc {};
    gmtime_r(&now, &utc);
    std::ostringstream out;
    out << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
    return out.str();
}

static auto platformMetadata() -> std::string {
    struct utsname info {};
    if (uname(&info) != 0) {
        throw std::runtime_error("uname failed");
    }
    std::ostringstream out;
    out << "os=" << info.sysname << ' ' << info.release << ' ' << info.version
        << "\narchitecture=" << info.machine << "\nlogicalCpus=" << sysconf(_SC_NPROCESSORS_ONLN) << '\n';
#ifdef __APPLE__
    std::array<char, 256> cpu {};
    std::size_t size = cpu.size();
    if (sysctlbyname("machdep.cpu.brand_string", cpu.data(), &size, nullptr, 0) != 0) {
        throw std::runtime_error("CPU sysctl failed");
    }
    std::uint64_t memory = 0;
    size = sizeof(memory);
    if (sysctlbyname("hw.memsize", &memory, &size, nullptr, 0) != 0) {
        throw std::runtime_error("memory sysctl failed");
    }
    vm_statistics64_data_t stats {};
    mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
    vm_size_t pageSize = 0;
    const auto host = mach_host_self();
    const auto status = host_statistics64(host, HOST_VM_INFO64, reinterpret_cast<host_info64_t>(&stats), &count);
    const auto pageStatus = host_page_size(host, &pageSize);
    mach_port_deallocate(mach_task_self(), host);
    if (status != KERN_SUCCESS || pageStatus != KERN_SUCCESS) {
        throw std::runtime_error("available memory query failed");
    }
    out << "cpu=" << cpu.data() << "\nphysicalMemoryBytes=" << memory
        << "\navailableMemoryBytesEstimate=" << (static_cast<std::uint64_t>(stats.free_count) + stats.inactive_count) * pageSize
        << "\navailableMemoryDefinition=free+inactive pages, snapshot before load\n";
#else
    out << "cpu=" << info.machine << "\nphysicalMemoryBytes=" << sysconf(_SC_PHYS_PAGES) * sysconf(_SC_PAGESIZE)
        << "\navailableMemoryBytesEstimate=" << sysconf(_SC_AVPHYS_PAGES) * sysconf(_SC_PAGESIZE) << '\n';
#endif
    return out.str();
}

static auto relativePath(const std::filesystem::path &path, const std::filesystem::path &directory) -> std::string {
    return path.lexically_relative(directory).generic_string();
}

static auto exportInput(const ExperimentConfig &config, const ExperimentInput &input) -> void {
    CsvRows manifest {{"chunkId", "file", "offset", "length"}};
    std::set<std::size_t> referenced;
    for (const auto &chunk : input.chunks) {
        referenced.insert(chunk.fileIndex);
        manifest.push_back({std::to_string(chunk.id), relativePath(input.files[chunk.fileIndex].path, config.outputDirectory),
                            std::to_string(chunk.offset), std::to_string(chunk.length)});
    }
    const auto bytes = encodeCsv(manifest);
    writeFile(config.outputDirectory / "manifest.csv", bytes);
    CsvRows checksums {{"file", "sha256"}, {"manifest.csv", sha256(bytes)}};
    CsvRows sourceChecksums {{"file", "bytes", "sha256"}};
    for (std::size_t i = 0; i < input.files.size(); ++i) {
        const auto &file = input.files[i];
        const auto path = relativePath(file.path, config.outputDirectory);
        sourceChecksums.push_back({path, std::to_string(file.bytes.size()), file.digest});
        if (referenced.contains(i)) {
            checksums.push_back({path, file.digest});
        }
    }
    writeFile(config.outputDirectory / "checksums.csv", encodeCsv(checksums));
    writeFile(config.outputDirectory / "source-checksums.csv", encodeCsv(sourceChecksums));
}

static auto parameterCsv() -> std::string {
    const auto parameters = approvedTransformParameters();
    CsvRows rows {{"i", "m_i", "a_i"}};
    for (std::size_t i = 0; i < FEATURE_COUNT; ++i) {
        std::ostringstream m;
        std::ostringstream a;
        m << "0x" << std::hex << std::setfill('0') << std::setw(8) << parameters.multipliers[i];
        a << "0x" << std::hex << std::setfill('0') << std::setw(8) << parameters.addends[i];
        rows.push_back({std::to_string(i), m.str(), a.str()});
    }
    const auto bytes = encodeCsv(rows);
    if (sha256(bytes) != PARAMETER_FILE_SHA256) {
        throw std::runtime_error("compiled parameters differ from versioned D-15 table");
    }
    return bytes;
}

static auto appendValue(std::string &bytes, const std::uint64_t value) -> void {
    for (std::size_t b = 0; b < FEATURE_BYTES; ++b) {
        bytes += static_cast<char>((value >> (b * 8)) & 0xffU);
    }
}

static auto outputDigest(const std::vector<SignatureResult> &results) -> std::string {
    std::string bytes;
    const auto size = checkedMultiply(results.size(), (FEATURE_COUNT + SUPER_FEATURE_COUNT) * FEATURE_BYTES);
    if (size > bytes.max_size()) {
        throw std::length_error("output digest buffer too large");
    }
    bytes.reserve(static_cast<std::size_t>(size));
    for (const auto &result : results) {
        if (result.status != ExtractStatus::Ok) {
            throw std::runtime_error("extractor failed on validated chunk");
        }
        for (const auto value : result.features) {
            appendValue(bytes, value);
        }
        for (const auto value : result.superFeatures) {
            appendValue(bytes, value);
        }
    }
    return sha256(bytes);
}

static auto extract(const std::size_t algorithm, const ChunkView chunk, const HashConfig &hash,
                     const TransformParameters &parameters) -> SignatureResult {
    return algorithm == 0 ? extractFinesse(chunk, hash) : extractNTransform(chunk, hash, parameters);
}

static auto runDiagnostics(const ExperimentConfig &config, const ExperimentInput &input,
                           const HashConfig &hash, const TransformParameters &parameters) -> void {
    CsvRows rows {{"algorithm", "profileId", "chunkId", "candidateId", "querySlot", "historicalSlot", "status"}};
    for (std::size_t algorithm = 0; algorithm < ALGORITHMS.size(); ++algorithm) {
        auto index = makeResemblanceIndex(ALGORITHMS[algorithm], PROFILE_ID);
        for (const auto &chunk : input.chunks) {
            if (chunk.length < FEATURE_COUNT) {
                continue;
            }
            const auto &bytes = input.files[chunk.fileIndex].bytes;
            const ChunkView view {reinterpret_cast<const std::uint8_t *>(bytes.data() + chunk.offset),
                                   static_cast<std::size_t>(chunk.length)};
            const auto signature = extract(algorithm, view, hash, parameters);
            const auto candidate = queryThenInsert(index, chunk.id, signature, ALGORITHMS[algorithm], PROFILE_ID);
            rows.push_back({std::string(ALGORITHMS[algorithm]), std::string(PROFILE_ID), std::to_string(chunk.id),
                candidate ? std::to_string(candidate->chunkId) : "", candidate ? std::to_string(candidate->querySlot) : "",
                candidate ? std::to_string(candidate->historicalSlot) : "", candidate ? "matched" : "unmatched"});
        }
    }
    writeFile(config.outputDirectory / "diagnostics.csv", encodeCsv(rows));
}

auto runExperiment(const ExperimentConfig &config) -> void {
    std::filesystem::create_directories(config.outputDirectory.parent_path());
    if (!std::filesystem::create_directory(config.outputDirectory)) {
        throw std::runtime_error("result directory already exists: " + config.outputDirectory.string());
    }
    std::string metadata;
    try {
        metadata = "timestamp=" + utcTimestamp() + "\ndocumentVersion=0.3+D-17+D-18+D-19+D-20\napprovals=D-01..D-12,D-14..D-20\n";
        for (const auto &[key, value] : config.values) {
            metadata += key + "=" + value + "\n";
        }
        metadata += "codeRevision=" + std::string(CODE_REVISION) + "\nsourceDigest=" + SOURCE_DIGEST
            + "\nsourceState=" + SOURCE_STATE + "\ncompiler=" + COMPILER_INFO + "\nbuildType=" + BUILD_TYPE
            + "\nbuildOptions=" + BUILD_OPTIONS + "\n" + platformMetadata();
        metadata += "threads=1\ninputDeduplicated=false\ninputMemoryMode=resident\n"
            "hashKind=Gear\nstateBits=64\ninitialValue=0\nupdatePosition=after-update\nsubchunkReset=false\n"
            "featureEncoding=uint64-le-4x8\nsuperFeatureHashProfile=gear64-zero-final\n"
            "subchunkPolicy=floor-full-coverage\nshortChunkPolicy=exclude-length-lt-12\n"
            "finesseFeatureBits=64\nnTransformFeatureBits=32\nnTransformModulus=2^32\n"
            "featureCount=" + std::to_string(FEATURE_COUNT) + "\nsuperFeatureCount=" + std::to_string(SUPER_FEATURE_COUNT)
            + "\nfeaturesPerSuperFeature=" + std::to_string(FEATURES_PER_SUPER_FEATURE) + "\n";
        metadata += "upstreamRevision=" + std::string(UPSTREAM_REVISION) + "\ngearTableSha256=" + GEAR_TABLE_SHA256
            + "\nparameterSha256=" + PARAMETER_FILE_SHA256 + "\n";
        const auto cdc = makeFastCDCConfig(8192, 2048, 0);
        metadata += "normalSize=" + std::to_string(cdc.NormalSize) + "\nmaskA=0x0000d90303530000\n"
            "maskS=0x0000d90303530000\nmaskL=0x0000d90303530000\n"
            "timing=steady_clock; full extraction + preallocated SignatureResult write; no I/O/CDC/index/digest\n"
            "order=warmup Finesse,NTransform; formal odd Finesse,NTransform; formal even NTransform,Finesse\n"
            "outputDigestEncoding=iteration/chunk row order; features then SF; each uint64 LE\n"
            "stddevDefinition=population\nMB=1000000 bytes\n";
        metadata += "inputDescriptor=" + config.inputPath.string() + "\ninputDescriptorSha256=" + sha256(readFile(config.inputPath)) + "\n";
        writeFile(config.outputDirectory / "run.txt", "status=running\n" + metadata);
        writeFile(config.outputDirectory / "parameters.csv", parameterCsv());
        writeFile(config.outputDirectory / "source-files.csv", SOURCE_FILES_CSV);
        const auto input = loadExperimentInput(config);
        exportInput(config, input);
        metadata += "manifestSha256=" + sha256(readFile(config.outputDirectory / "manifest.csv"))
            + "\nchecksumsSha256=" + sha256(readFile(config.outputDirectory / "checksums.csv"))
            + "\nsourceChecksumsSha256=" + sha256(readFile(config.outputDirectory / "source-checksums.csv"))
            + "\ncdcElapsedNs=" + std::to_string(input.cdcElapsedNs) + "\n";
        const auto effectiveBytes = input.inputBytes - input.excludedBytes;
        const auto validCount = input.chunks.size() - static_cast<std::size_t>(input.excludedChunks);
        metadata += "inputBytes=" + std::to_string(input.inputBytes) + "\neffectiveBytes=" + std::to_string(effectiveBytes)
            + "\nchunkCount=" + std::to_string(input.chunks.size()) + "\nexcludedChunks=" + std::to_string(input.excludedChunks)
            + "\nexcludedBytes=" + std::to_string(input.excludedBytes) + "\n";
        if (validCount == 0 || effectiveBytes == 0) {
            throw std::invalid_argument("no supported chunks after excluding length < 12");
        }
        std::vector<ChunkView> views;
        views.reserve(validCount);
        for (const auto &chunk : input.chunks) {
            if (chunk.length >= FEATURE_COUNT) {
                views.push_back({reinterpret_cast<const std::uint8_t *>(input.files[chunk.fileIndex].bytes.data() + chunk.offset),
                                 static_cast<std::size_t>(chunk.length)});
            }
        }
        const auto processedBytes = checkedMultiply(effectiveBytes, config.iterations);
        const auto outputCount = checkedMultiply(validCount, config.iterations);
        std::vector<SignatureResult> output;
        if (outputCount > output.max_size()) {
            throw std::length_error("measurement output buffer too large");
        }
        output.resize(static_cast<std::size_t>(outputCount));
        const HashConfig hash;
        const auto parameters = approvedTransformParameters();
        CsvRows measurements {{"runId", "algorithm", "profileId", "repetition", "order", "inputBytes", "processedBytes",
            "excludedBytes", "chunkCount", "excludedChunks", "elapsedNs", "outputDigest", "status"}};
        std::array<std::array<double, MEASUREMENT_RUNS>, 2> seconds {};
        std::array<std::string, 2> expectedDigests;
        for (std::size_t repetition = 0; repetition <= MEASUREMENT_RUNS; ++repetition) {
            for (std::size_t order = 0; order < ALGORITHMS.size(); ++order) {
                const auto algorithm = repetition > 0 && repetition % 2 == 0 ? 1 - order : order;
                const auto start = std::chrono::steady_clock::now();
                std::size_t position = 0;
                for (std::uint64_t iteration = 0; iteration < config.iterations; ++iteration) {
                    for (const auto view : views) {
                        output[position++] = extract(algorithm, view, hash, parameters);
                    }
                }
                const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now() - start).count();
                const auto digest = outputDigest(output);
                if (repetition == 0) {
                    expectedDigests[algorithm] = digest;
                } else {
                    if (elapsed <= 0 || digest != expectedDigests[algorithm]) {
                        throw std::runtime_error("non-positive timing or inconsistent output digest");
                    }
                    seconds[algorithm][repetition - 1] = static_cast<double>(elapsed) / 1e9;
                    measurements.push_back({config.values.at("runId"), std::string(ALGORITHMS[algorithm]), std::string(PROFILE_ID),
                        std::to_string(repetition), std::to_string(order + 1), std::to_string(input.inputBytes),
                        std::to_string(processedBytes), std::to_string(input.excludedBytes), std::to_string(input.chunks.size()),
                        std::to_string(input.excludedChunks), std::to_string(elapsed), digest, "ok"});
                    writeFile(config.outputDirectory / "measurements.csv", encodeCsv(measurements));
                }
            }
        }
        if (config.diagnostics) {
            runDiagnostics(config, input, hash, parameters);
        }
        std::array<double, 2> means {};
        std::array<double, 2> meanSpeeds {};
        for (std::size_t a = 0; a < ALGORITHMS.size(); ++a) {
            for (const auto elapsed : seconds[a]) {
                means[a] += elapsed / MEASUREMENT_RUNS;
                meanSpeeds[a] += (static_cast<double>(processedBytes) / elapsed) / MEASUREMENT_RUNS;
            }
        }
        CsvRows summary {{"algorithm", "repetitions", "processedBytesPerMeasurement", "meanSeconds", "stddevSeconds",
                          "meanBytesPerSecond", "stddevBytesPerSecond", "meanMBPerSecond", "meanBaselineSecondsOverMeanFinesseSeconds"}};
        for (std::size_t a = 0; a < ALGORITHMS.size(); ++a) {
            double variance = 0;
            double speedVariance = 0;
            for (const auto elapsed : seconds[a]) {
                variance += std::pow(elapsed - means[a], 2) / MEASUREMENT_RUNS;
                speedVariance += std::pow(static_cast<double>(processedBytes) / elapsed - meanSpeeds[a], 2) / MEASUREMENT_RUNS;
            }
            summary.push_back({std::string(ALGORITHMS[a]), std::to_string(MEASUREMENT_RUNS), std::to_string(processedBytes),
                number(means[a]), number(std::sqrt(variance)), number(meanSpeeds[a]), number(std::sqrt(speedVariance)),
                number(meanSpeeds[a] / 1e6), number(means[1] / means[0])});
        }
        writeFile(config.outputDirectory / "summary.csv", encodeCsv(summary));
        writeFile(config.outputDirectory / "run.txt", "status=ok\n" + metadata);
    } catch (const std::exception &error) {
        std::error_code ignored;
        std::filesystem::remove(config.outputDirectory / "summary.csv", ignored);
        try {
            writeFile(config.outputDirectory / "run.txt", "status=failed\nreason=" + std::string(error.what()) + "\n" + metadata);
        } catch (...) {
            // 不掩盖原始失败；主程序仍返回非零。
        }
        throw;
    }
}
