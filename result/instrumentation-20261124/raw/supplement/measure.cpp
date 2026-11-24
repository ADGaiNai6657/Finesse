#include "EncodedStore/EncodedStore.h"
#include "Finesse/Finesse.h"
#include <chrono>
#include <iostream>
#include <string_view>

// 按实际命中的 NTransform-Gear 候选比较 12 个对应子块。
static void locality(const ExperimentInput &input, const EncodedStore &store) {
    std::cout << "targetId,baseId,identicalMask,equalFeatureMask,identicalCount,equalFeatureCount\n";
    for (const auto &attempt : store.attempts) {
        const auto a = storedChunkView(input.store, attempt.id);
        const auto b = storedChunkView(input.store, attempt.candidate.chunkId);
        const auto fa = extractFinesse(a, {});
        const auto fb = extractFinesse(b, {});
        if (fa.status != ExtractStatus::Ok || fb.status != ExtractStatus::Ok)
            throw std::runtime_error("invalid candidate signature");
        std::string identical, equalFeature;
        unsigned contentCount = 0, featureCount = 0;
        const auto slice = [](ChunkView block, std::size_t i) {
            auto begin = subchunkBoundary(block.length, i);
            return std::string_view(reinterpret_cast<const char *>(block.data + begin),
                                    subchunkBoundary(block.length, i + 1) - begin);
        };
        for (std::size_t i = 0; i < FEATURE_COUNT; ++i) {
            bool same = sha1(slice(a, i)) == sha1(slice(b, i));
            bool equal = fa.features[i] == fb.features[i];
            identical += same ? '1' : '0';
            equalFeature += equal ? '1' : '0';
            contentCount += same;
            featureCount += equal;
        }
        std::cout << attempt.id << ',' << attempt.candidate.chunkId << ',' << identical << ','
                  << equalFeature << ',' << contentCount << ',' << featureCount << '\n';
    }
}

// 在单算法进程中复用生产加载、分块和编码池构建。
int main(int argc, char **argv) {
    if (argc != 4) return 2;
    const auto config = loadExperimentConfig(argv[1]);
    const std::string algorithm = argv[2], mode = argv[3];
    metrics::Collector collector(metrics::Mode::Benchmark);
    const metrics::Activate active(collector);
    const auto begin = metrics::Clock::now();
    const auto input = loadExperimentInput(config);
    const auto store = buildEncodedStore(input, algorithm);
    const auto end = metrics::Clock::now();
    if (mode == "locality") {
        locality(input, store);
    } else if (mode == "throughput") {
        std::uint64_t payload = 0;
        for (const auto &record : store.chunks) payload += record.payload.size();
        const auto stageUs = [&](const metrics::Collector::Stage &key) -> std::string {
            const auto found = collector.stages.find(key);
            return found == collector.stages.end() ? "" : std::to_string(found->second.total);
        };
        std::cout << input.inputBytes << ',' << input.chunks.size() << ',' << store.chunks.size() << ','
                  << payload << ',' << std::chrono::duration_cast<std::chrono::nanoseconds>(end-begin).count()
                  << ',' << stageUs({"FastCDC", "input", "cdc_total"})
                  << ',' << stageUs({algorithm, "build", "delta_encode"}) << '\n';
    } else return 2;
}
