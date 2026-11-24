#include "Run.h"
#include "Platform.h"

#include "BuildInfo.h"
#include "EncodedStore/EncodedStore.h"
#include "Experiment/IO.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
namespace metrics {
// 将标签和文本转成 JSON 字符串，转义控制字符。
static auto quote(const std::string_view value) -> std::string {
    std::ostringstream json;
    json << '"';
    for (const unsigned char ch : value) {
        if (ch == '"' || ch == '\\') json << '\\' << ch;
        else if (ch < 32) json << "\\u" << std::hex << std::setfill('0') << std::setw(4) << unsigned(ch);
        else json << ch;
    }
    json << '"';
    return json.str();
}

// 将解析后的配置键值序列化为 JSON 对象。
static auto object(const std::map<std::string, std::string> &values) -> std::string {
    std::string json = "{";
    for (const auto &[key, value] : values) {
        if (json.size() > 1) json += ',';
        json += quote(key) + ':' + quote(value);
    }
    return json + '}';
}

// 将浮点指标格式化为可供程序读取的十进制文本。
static auto number(const double value) -> std::string {
    std::ostringstream out;
    out << std::setprecision(15) << value;
    return out.str();
}

// 初始化本次采集，并在运行开头保存完整的解析配置和环境。
Run::Run(const Mode mode, std::filesystem::path output, const std::string_view kind,
         const std::map<std::string, std::string> &config, const std::map<std::string, std::string> &inputConfig)
    : started(mode == Mode::Off ? "" : utcTimestamp()), collector(mode), activation(collector), directory(std::move(output)),
      kind(kind), cpuStart(mode == Mode::Off ? std::nullopt : resourceSnapshot().cpuTimeUs) {
    if (mode == Mode::Off) return;
    // runId 可被人类复用，追加时间和 PID 区分实际进程运行。
    const auto nonce = std::chrono::system_clock::now().time_since_epoch().count();
    id = config.at("runId") + "-" + std::to_string(nonce) + "-" + std::to_string(processId());
    const auto host = hostInfo();
    std::string hardware;
    if (!host.cpu.empty()) hardware += ",\"cpu\":" + quote(host.cpu);
    if (host.physicalMemory) hardware += ",\"physical_memory_bytes\":" + std::to_string(*host.physicalMemory);
    const auto configuration = "{\"schema_version\":1,\"experiment_id\":" + quote(id)
        + ",\"mode\":" + quote(mode == Mode::Profiling ? "profiling" : "benchmark")
        + ",\"started_utc\":" + quote(started) + ",\"kind\":" + quote(kind)
        + ",\"git_commit\":" + quote(CODE_REVISION) + ",\"source_digest\":" + quote(SOURCE_DIGEST)
        + ",\"compiler\":" + quote(COMPILER_INFO) + ",\"build_type\":" + quote(BUILD_TYPE)
        + ",\"build_options\":" + quote(BUILD_OPTIONS) + ",\"os\":" + quote(host.os)
        + ",\"architecture\":" + quote(host.architecture) + hardware + ",\"threads\":1,\"config\":" + object(config)
        + ",\"input_config\":" + object(inputConfig) + "}\n";
    writeFile(directory / "experiment_config.json", configuration);
}

// 在原文池有效时复制文件和块的元信息，不保留原文字节。
void Run::captureInput(const ExperimentInput &input) {
    if (collector.mode == Mode::Off) return;
    fileCount = input.files.size();
    inputBytes = input.inputBytes;
    chunks = input.chunks.size();
    unique = input.store.chunks.size();
    // 文件计数使用出现记录。每个存储 ID 只在首次出现时登记所属文件。
    std::vector<std::array<std::uint64_t, 3>> counts(fileCount);
    if (kind == "delta") firstFileByStoredId.resize(unique);
    for (const auto &chunk : input.chunks) {
        auto &countsForFile = counts.at(chunk.fileIndex);
        ++countsForFile[0];
        ++countsForFile[chunk.isDuplicate ? 2 : 1];
        if (!chunk.isDuplicate && kind == "delta") firstFileByStoredId.at(chunk.storedId) = chunk.fileIndex;
        if (collector.mode == Mode::Profiling) {
            const auto &stored = readStoredChunk(input.store, chunk.storedId);
            chunkRows.push_back({id, std::to_string(chunk.id), std::to_string(chunk.fileIndex), std::to_string(chunk.offset),
                std::to_string(chunk.length), std::to_string(chunk.storedId), sha1Hex(stored.hash),
                chunk.isDuplicate ? "true" : "false"});
        }
    }
    for (std::size_t i = 0; i < input.files.size(); ++i) {
        const auto &file = input.files[i];
        sourceBytes += file.length;
        // 用显式 UTF-8 处理 Windows 非 ASCII 路径。
        const auto path = file.path.lexically_relative(directory).generic_u8string();
        const auto extension = file.path.extension().generic_u8string();
        fileRows.push_back({id, std::to_string(i), std::string(path.begin(), path.end()),
            std::string(extension.begin(), extension.end()), std::to_string(file.length), file.digest,
            std::to_string(counts[i][0]), std::to_string(counts[i][1]), std::to_string(counts[i][2])});
    }
}

// 追加恢复证据，空摘要表示输入范围不足以做整文件校验。
void Run::recordRecovery(const std::string_view algorithm, const std::size_t file, const std::uint64_t bytes,
                         const std::string_view expected, const std::string_view actual, const bool ok) {
    recoveryRows.push_back({ std::string(algorithm), std::to_string(file), actual.empty() ? "" : std::to_string(bytes),
        std::string(expected), std::string(actual), actual.empty() ? "not_applicable" : (ok ? "ok" : "failed")});
}

// 正确性证据独立于模式。关闭采集时仍执行相同的恢复及核验。
auto Run::exportRecoveryEvidence() const -> bool {
    if (recoveryRows.empty()) return false;
    CsvRows rows {{"algorithm", "file_id", "restored_bytes", "expected_sha256", "restored_sha256", "status"}};
    rows.insert(rows.end(), recoveryRows.begin(), recoveryRows.end());
    writeFile(directory / "file-restoration.csv", encodeCsv(rows));
    return true;
}

// 按算法汇总真实 Raw/Delta payload，并按首次出现归属到文件。
void Run::captureStorage(const EncodedStore &store) {
    if (collector.mode == Mode::Off) return;
    // 共享块的 payload 只归到第一次出现的文件，避免跨文件重复计费。
    std::vector<std::array<std::uint64_t, 2>> perFile(fileCount);
    std::uint64_t raw = 0, delta = 0, original = 0, raws = 0, deltas = 0;
    for (const auto &chunk : store.chunks) {
        original += chunk.originalLength;
        const bool isRaw = chunk.kind == PayloadKind::Raw;
        const auto size = chunk.payload.size();
        const auto file = firstFileByStoredId.at(chunk.id);
        perFile[file][isRaw ? 0 : 1] += size;
        if (isRaw) {
            raw += size;
            ++raws;
        } else {
            delta += size;
            ++deltas;
        }
        if (collector.mode == Mode::Profiling) {
            chunkStorageRows.push_back({id, store.algorithm, std::to_string(chunk.id),
                std::to_string(chunk.firstChunkId), std::to_string(file), isRaw ? "Raw" : "Delta",
                std::to_string(size), chunk.baseId ? std::to_string(*chunk.baseId) : ""});
        }
    }
    for (std::size_t file = 0; file < perFile.size(); ++file) {
        const auto [rawBytes, deltaBytes] = perFile[file];
        fileStorageRows.push_back({id, store.algorithm, std::to_string(file), std::to_string(rawBytes),
            std::to_string(deltaBytes), std::to_string(rawBytes + deltaBytes)});
    }
    const auto encoded = raw + delta;
    storageRows.push_back({id, store.algorithm, std::to_string(original), std::to_string(raw), std::to_string(delta),
        std::to_string(encoded), std::to_string(raws), std::to_string(deltas),
        encoded ? number(double(original) / encoded) : "", original ? number(double(original - encoded) / original) : ""});
}

// 按 CSV 契约把无适用 ID 映射为空字段。
static auto idField(const std::uint64_t value) -> std::string {
    return value == NoId ? "" : std::to_string(value);
}

// 冻结采集并输出阶段、资源与完成状态。失败也尽量落盘。
void Run::finish(const bool success, const std::string_view reason) {
    if (collector.mode == Mode::Off) return;
    // 阶段聚合在导出前冻结，墙钟和资源快照在主要统计文件落盘后采集。
    Collector disabled(Mode::Off);
    const Activate suspend(disabled);
    const auto processingWall = Microseconds(Clock::now() - start).count();
    exportTimings();
    exportOperations();
    exportAlgorithmMetrics();
    exportIndices();
    const auto wall = Microseconds(Clock::now() - start).count();
    const auto resources = resourceSnapshot();
    std::optional<double> cpu;
    if (resources.cpuTimeUs && cpuStart) cpu = *resources.cpuTimeUs - *cpuStart;
    const auto peak = resources.peakRssBytes ? std::to_string(*resources.peakRssBytes) : "";
    writeFile(directory / "system_metrics.csv", encodeCsv({{"experiment_id","wall_time_us","wall_time_ms","cpu_time_us",
        "cpu_time_ms","cpu_percent_one_core","process_peak_rss_bytes","threads","processing_wall_time_us","processing_wall_time_ms"},
        {id,number(wall),number(wall / 1000),cpu ? number(*cpu) : "",cpu ? number(*cpu / 1000) : "",
            cpu && wall > 0 ? number(*cpu / wall * 100) : "",peak,"1",number(processingWall),number(processingWall / 1000)}}));
    const double throughput = wall > 0 ? inputBytes * 1e6 / wall : 0;
    // JSON 汇总只引用已经采集的逻辑 payload，分母为零的比率留空。
    std::string storage = "[";
    for (const auto &row : storageRows) {
        if (storage.size() > 1) storage += ',';
        storage += "{\"algorithm\":" + quote(row[1]) + ",\"logical_encoded_bytes\":" + row[5]
            + ",\"dcr\":" + (row[8].empty() ? "null" : row[8]) + '}';
    }
    storage += ']';
    const auto verified = std::count_if(recoveryRows.begin(), recoveryRows.end(),
        [](const auto &row) { return row.back() == "ok"; });
    const auto failed = std::count_if(recoveryRows.begin(), recoveryRows.end(),
        [](const auto &row) { return row.back() == "failed"; });
    std::string stages = "[";
    for (const auto &[key, timing] : collector.stages) {
        if (stages.size() > 1) stages += ',';
        const auto &[algorithm, phase, name] = key;
        stages += "{\"algorithm\":" + quote(algorithm) + ",\"phase\":" + quote(phase) + ",\"stage_name\":"
            + quote(name) + ",\"total_time_us\":" + number(timing.total) + ",\"total_time_ms\":" + number(timing.total / 1000) + '}';
    }
    stages += ']';
    writeFile(directory / "summary.json", "{\"schema_version\":1,\"experiment_id\":" + quote(id)
        + ",\"kind\":" + quote(kind) + ",\"ended_utc\":" + quote(utcTimestamp())
        + ",\"complete\":" + (success && collector.complete ? "true" : "false") + ",\"reason\":" + quote(reason)
        + ",\"files\":" + std::to_string(fileCount) + ",\"input_bytes\":" + std::to_string(inputBytes)
        + ",\"source_bytes\":" + std::to_string(sourceBytes) + ",\"chunks\":" + std::to_string(chunks)
        + ",\"unique_chunks\":" + std::to_string(unique) + ",\"duplicates\":" + std::to_string(chunks - unique)
        + ",\"wall_time_us\":" + number(wall) + ",\"wall_time_ms\":" + number(wall / 1000)
        + ",\"processing_wall_time_us\":" + number(processingWall)
        + ",\"processing_wall_time_ms\":" + number(processingWall / 1000)
        + ",\"input_bytes_per_second\":" + (wall > 0 ? number(throughput) : "null")
        + ",\"process_peak_rss_bytes\":" + (peak.empty() ? "null" : peak) + ",\"file_sha256_passes\":" + std::to_string(verified)
        + ",\"file_sha256_failures\":" + std::to_string(failed) + ",\"file_sha256_pass_rate\":"
        + (verified + failed ? number(double(verified) / (verified + failed)) : "null")
        + ",\"storage\":" + storage + ",\"stages\":" + stages + ",\"outputs\":" + exportedFiles() + "}\n");
}

// 阶段表记录全部调用。分位数仅对 Profiling 的有界样本计算。
void Run::exportTimings() {
    CsvRows stage {{"experiment_id", "algorithm", "phase", "stage_name", "call_count", "failure_count",
        "input_bytes", "output_bytes", "total_time_us", "total_time_ms", "average_time_us", "min_time_us", "max_time_us",
        "sample_count", "p50_time_us", "p95_time_us", "p99_time_us"}};
    // 对有限样本求最近秩分位数，累计时间等指标仍取全量。
    for (auto &[key, timing] : collector.stages) {
        const auto &[algorithm, phase, name] = key;
        std::sort(timing.samples.begin(), timing.samples.end());
        const auto percentile = [&](const double p) {
            return timing.samples.empty() ? "" : number(timing.samples[std::size_t(std::ceil(p * timing.samples.size())) - 1]);
        };
        stage.push_back({id, std::string(algorithm), std::string(phase), std::string(name),
            std::to_string(timing.count), std::to_string(timing.failures), std::to_string(timing.inputBytes),
            std::to_string(timing.outputBytes), number(timing.total), number(timing.total / 1000),
            number(timing.total / timing.count), number(timing.min), number(timing.max), std::to_string(timing.samples.size()),
            percentile(.50), percentile(.95), percentile(.99)});
    }
    writeFile(directory / "stage_timing.csv", encodeCsv(stage));
    const auto writeRows = [&](const char *name, std::vector<std::string> header, const CsvRows &rows) {
        if (rows.empty()) return;
        std::ofstream out(directory / name, std::ios::binary);
        out.exceptions(std::ios::failbit | std::ios::badbit);
        out << encodeCsv({std::move(header)});
        for (const auto &row : rows) out << encodeCsv({row});
        out.close();
    };
    writeRows("file_metrics.csv", {"experiment_id","file_id","path","extension","bytes","sha256","chunks","unique_first_seen","duplicates"}, fileRows);
    writeRows("chunk_metrics.csv", {"experiment_id","chunk_id","file_id","offset","bytes","stored_id","sha1","duplicate"}, chunkRows);
    writeRows("storage_metrics.csv", {"experiment_id","algorithm","unique_original_bytes","raw_bytes","delta_bytes",
        "logical_encoded_bytes","raw_chunks","delta_chunks","dcr","saving_ratio"}, storageRows);
    if (!recoveryRows.empty()) {
        CsvRows joined;
        joined.reserve(recoveryRows.size());
        for (const auto &row : recoveryRows) {
            auto &output = joined.emplace_back(row);
            output.insert(output.begin(), id);
        }
        writeRows("recovery_metrics.csv", {"experiment_id","algorithm","file_id","restored_bytes",
            "expected_sha256","restored_sha256","status"}, joined);
    }
    CsvRows files;
    for (const auto &[key, timing] : collector.files) {
        const auto &[file, algorithm, phase, name] = key;
        files.push_back({id,std::to_string(file),std::string(algorithm),std::string(phase),std::string(name),std::to_string(timing.count),
            std::to_string(timing.failures),number(timing.total),number(timing.total / 1000)});
    }
    writeRows("file_storage_metrics.csv", {"experiment_id","algorithm","file_id","raw_bytes","delta_bytes",
        "first_seen_payload_bytes"}, fileStorageRows);
    writeRows("chunk_storage_metrics.csv", {"experiment_id","algorithm","stored_id","first_chunk_id","first_file_id",
        "storage_type","payload_bytes","base_stored_id"}, chunkStorageRows);
    writeRows("file_stage_timing.csv", {"experiment_id","file_id","algorithm","phase","stage_name","call_count",
        "failure_count","total_time_us","total_time_ms"}, files);
}

// 逐条编码 CSV，避免再分配一份与明细表等大的文本缓冲。
void Run::exportOperations() const {
    if (!collector.operations.empty()) {
        std::ofstream out(directory / "operation_metrics.csv", std::ios::binary);
        out.exceptions(std::ios::failbit | std::ios::badbit);
        out << "experiment_id,operation_id,operation_type,algorithm,phase,file_id,chunk_id,duration_us,duration_ms,result,input_bytes,output_bytes,related_chunk_id\n";
        std::uint64_t next = 0;
        for (const auto &op : collector.operations) {
            out << encodeCsv({{id,std::to_string(next++),std::string(op.stage),std::string(op.context.algorithm),
                std::string(op.context.phase),idField(op.context.file),idField(op.context.chunk),number(op.elapsed),
                number(op.elapsed / 1000),std::string(op.result),idField(op.inputBytes),
                idField(op.outputBytes),idField(op.relatedChunk)}});
        }
        out.close();
    }
}

// 通用计数器使用固定描述。自定义计数器可由算法注册自己的单位与含义。
void Run::exportAlgorithmMetrics() const {
    if (collector.counters.empty()) return;
    static const std::map<std::string_view, std::string_view> builtIn {
        {"boundaries", "FastCDC cuts including the final file boundary"},
        {"sha1_index_entries", "final number of unique SHA-1 index entries"},
        {"sha1_hit", "exact duplicate hits after length and byte comparison"},
        {"sha1_miss", "chunks absent from the exact-deduplication index"},
        {"sha1_insert", "SHA-1 index insertions"},
        {"sf_index_entries", "distinct SF keys at the end of the phase"},
        {"sf_hit", "candidate queries returning a FirstFit match"},
        {"sf_miss", "candidate queries without a match"},
        {"sf_insert", "chunk signature insertion calls"},
        {"sf_slot_0_hits", "matches found at query slot zero"},
        {"sf_slot_1_hits", "matches found at query slot one"},
        {"sf_slot_2_hits", "matches found at query slot two"},
        {"sf_extractions", "full SF extraction calls in completed rounds"},
        {"raw_read", "Raw occurrence reads in recovery"},
        {"delta_decode", "successfully decoded Delta occurrences in recovery"}
    };
    std::ofstream out(directory / "algorithm_metrics.jsonl", std::ios::binary);
    out.exceptions(std::ios::failbit | std::ios::badbit);
    for (const auto &[key, value] : collector.counters) {
        const auto &[algorithm, phase, name] = key;
        const auto registered = collector.definitions.find({algorithm, name});
        const auto standard = builtIn.find(name);
        const auto definition = registered != collector.definitions.end() ? registered->second
            : CounterDefinition {"count", standard != builtIn.end() ? standard->second : name};
        out << "{\"experiment_id\":" << quote(id) << ",\"algorithm\":" << quote(algorithm)
            << ",\"entity_type\":\"phase\",\"entity_id\":" << quote(phase) << ",\"metric_name\":" << quote(name)
            << ",\"metric_type\":\"counter\",\"metric_value\":" << value << ",\"unit\":" << quote(definition.unit)
            << ",\"description\":" << quote(definition.description) << "}\n";
    }
    out.close();
}

// 将已执行的索引操作和实际条目数输出为统一表。
void Run::exportIndices() const {
    CsvRows indices {{"experiment_id","algorithm","phase","index_type","queries","hits","misses","insert_calls",
                     "final_entries","query_us","insert_us","initialize_us"}};
    for (const auto &[key, value] : collector.counters) {
        const auto &[algorithm, phase, name] = key;
        if (name != "sf_index_entries" && name != "sha1_index_entries") continue;
        const auto prefix = name == "sf_index_entries" ? "sf_" : "sha1_";
        const auto get = [&](const std::string_view metric) {
            const auto found = collector.counters.find({algorithm,phase,metric});
            return found == collector.counters.end() ? std::uint64_t{0} : found->second;
        };
        const auto time = [&](const std::string_view stage) {
            const auto found = collector.stages.find({algorithm,phase,stage});
            return found == collector.stages.end() ? "" : number(found->second.total);
        };
        const auto hit = get(std::string(prefix) + "hit");
        const auto miss = get(std::string(prefix) + "miss");
        indices.push_back({id,std::string(algorithm),std::string(phase),prefix == std::string_view("sf_") ? "super_feature" : "sha1",
            std::to_string(hit + miss),std::to_string(hit),std::to_string(miss),
            std::to_string(get(std::string(prefix) + "insert")),std::to_string(value),
            time(std::string(prefix) + "lookup"),time(std::string(prefix) + "insert"),time("sf_index_initialize")});
    }
    if (indices.size() > 1) writeFile(directory / "index_metrics.csv", encodeCsv(indices));
}

// 清单只列出本次成功写出的统计文件。
auto Run::exportedFiles() const -> std::string {
    std::string outputs = "[";
    for (const auto *name : {"experiment_config.json","stage_timing.csv","file_metrics.csv","file_stage_timing.csv",
        "chunk_metrics.csv","chunk_storage_metrics.csv","file_storage_metrics.csv","operation_metrics.csv","index_metrics.csv","cache_metrics.csv","storage_metrics.csv",
        "recovery_metrics.csv","file-restoration.csv","system_metrics.csv","algorithm_metrics.jsonl"}) {
        if (!std::filesystem::exists(directory / name)) continue;
        if (outputs.size() > 1) outputs += ',';
        outputs += quote(name);
    }
    return outputs + ']';
}

} // namespace metrics
