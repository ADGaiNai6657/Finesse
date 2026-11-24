#include "EncodedStore/EncodedStore.h"
#include "Finesse/Finesse.h"
#include <chrono>
#include <iostream>
#include <string_view>

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

int main(int argc, char **argv) {
    if (argc != 4) return 2;
    const auto config = loadExperimentConfig(argv[1]);
    const std::string algorithm = argv[2], mode = argv[3];
    const auto begin = std::chrono::steady_clock::now();
    const auto input = loadExperimentInput(config);
    const auto store = buildEncodedStore(input, algorithm);
    const auto end = std::chrono::steady_clock::now();
    if (mode == "locality") {
        locality(input, store);
    } else if (mode == "throughput") {
        std::uint64_t payload = 0;
        for (const auto &record : store.chunks) payload += record.payload.size();
        std::cout << input.inputBytes << ',' << input.chunks.size() << ',' << store.chunks.size() << ','
                  << payload << ',' << std::chrono::duration_cast<std::chrono::nanoseconds>(end-begin).count() << '\n';
    } else return 2;
}
