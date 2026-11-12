#!/usr/bin/env python3
"""Independent audit of Delta CSVs against source ranges; does not load persisted payloads.

Actual payload restoration runs inside C++ before process exit. This audit checks the
resulting restoration hashes, reference graph, source hashes and all quality metrics.
"""
import collections
import csv
import hashlib
import math
from pathlib import Path
import sys


#读取带表头的 CSV 产物
#输入：path：CSV 文件路径。
#输出：返回按行排列的字典列表，字段保留为字符串；读取或解析失败时抛异常。
def rows(path):
    with path.open(newline="", encoding="utf-8") as stream:
        return list(csv.DictReader(stream))


#检查集成测试或审计条件
#输入：condition：必须成立的条件；message：失败时的说明。
#输出：返回 None；条件为假时抛 AssertionError。
def check(condition, message):
    if not condition:
        raise AssertionError(message)


#读取运行记录中的 key=value 元信息
#输入：path：run.txt 等文本文件路径。
#输出：返回键值字典，忽略不含等号的行；值中的等号保留，重复键以最后一次为准。
def metadata(path):
    return dict(line.split("=", 1) for line in path.read_text().splitlines() if "=" in line)


#独立核对 Delta 产物、源范围摘要、引用资格和加权指标
#输入：directory：已完成的 Delta 结果目录。
#输出：返回两算法 compression-summary 字典列表；任何状态、摘要、引用或计数不符均失败，不从 CSV 重载 payload。
def verify_delta_results(directory):
    directory = Path(directory).resolve()
    meta = metadata(directory / "run.txt")
    check(meta["status"] == meta["restorationStatus"] == "ok", "run failed")
    check(meta["sourcePoolReleasedBeforeRestoration"] == "true", "source pool not released")
    check(meta["sourceFilesReadDuringRestoration"] == "false", "restoration used source files")
    for key, digest in meta.items():
        if key.endswith(".csv.sha256"):
            check(hashlib.sha256((directory / key[:-7]).read_bytes()).hexdigest() == digest, f"artifact checksum: {key}")
    for name, key in (("source-files.csv", "sourceDigest"), ("xdelta-source-files.csv", "xdeltaSourceManifestSha256"), ("parameters.csv", "parameterSha256")):
        check(hashlib.sha256((directory / name).read_bytes()).hexdigest() == meta[key], f"build provenance {name}")
    sources = rows(directory / "source-checksums.csv")
    for source in sources:
        content = (directory / source["file"]).read_bytes()
        check(len(content) == int(source["bytes"]) and hashlib.sha256(content).hexdigest() == source["sha256"], "source integrity")
    manifest = rows(directory / "manifest.csv")
    refs = rows(directory / "chunk-references.csv")
    occurrences = rows(directory / "occurrences.csv")
    stored = rows(directory / "stored-chunks.csv")
    check(len(manifest) == len(refs) == len(occurrences), "occurrence count")
    unique, expected, canonical, counts = {}, {}, {}, collections.Counter()
    current_path, content = None, None
    for source, ref, occurrence in zip(manifest, refs, occurrences):
        path = directory / source["file"]
        if path != current_path:
            content, current_path = path.read_bytes(), path
        offset, length = int(source["offset"]), int(source["length"])
        payload = content[offset:offset + length]
        check(len(payload) == length, "source range")
        digest = hashlib.sha256(payload).hexdigest()
        key = (length, digest)
        duplicate = key in unique
        if not duplicate:
            unique[key] = len(unique)
            canonical[len(unique) - 1] = (source["chunkId"], length, digest, hashlib.sha1(payload).hexdigest())
        stored_id = unique[key]
        check(int(ref["storedId"]) == int(occurrence["storedId"]) == stored_id, "canonical identity")
        check(all(ref[k] == source[k] for k in ("chunkId", "file", "offset", "length")), "source reference changed")
        check(all(occurrence[k] == source[k] for k in ("chunkId", "offset", "length")), "occurrence source changed")
        check(int(occurrence["fileIndex"]) < len(sources), "fileIndex outside sources")
        check(sources[int(occurrence["fileIndex"])]["file"] == source["file"], "fileIndex identifies wrong source")
        check(ref["isDuplicate"] == occurrence["isDuplicate"] == str(duplicate).lower(), "duplicate status")
        expected[source["chunkId"]] = (stored_id, length, digest)
        counts[stored_id] += 1
    check(len(stored) == len(canonical), "unique cardinality")
    for record in stored:
        stored_id = int(record["storedId"])
        first, length, digest, sha1 = canonical[stored_id]
        check(record["firstChunkId"] == first and int(record["length"]) == length, "first appearance/length")
        check(record["sha1"] == sha1 and int(record["referenceCount"]) == counts[stored_id], "dedup hash/count")
    original_bytes = sum(v[1] for v in canonical.values())
    encoded = rows(directory / "encoded-chunks.csv")
    attempts = rows(directory / "delta-attempts.csv")
    verification = rows(directory / "restoration-verification.csv")
    summaries = rows(directory / "compression-summary.csv")
    check([s["algorithm"] for s in summaries] == ["Finesse-Gear", "NTransform-Gear"], "algorithm set/order")
    for summary in summaries:
        algorithm = summary["algorithm"]
        records = [r for r in encoded if r["algorithm"] == algorithm]
        tries = [r for r in attempts if r["algorithm"] == algorithm]
        restored = [r for r in verification if r["algorithm"] == algorithm]
        by_id = {int(r["storedId"]): r for r in records}
        check(len(records) == len(by_id) == len(canonical), "one encoded record per unique chunk")
        check([r["chunkId"] for r in restored] == [r["chunkId"] for r in manifest], "restoration missing/reordered occurrences")
        for result in restored:
            stored_id, length, digest = expected[result["chunkId"]]
            check(int(result["storedId"]) == stored_id and int(result["bytes"]) == length, "restored identity/length")
            check(result["restoredSha256"] == digest and result["status"] == "ok", "restored content digest")
        for record in records:
            stored_id = int(record["storedId"])
            first, length, digest, sha1 = canonical[stored_id]
            check(record["firstChunkId"] == first and int(record["originalBytes"]) == length, "encoded provenance")
            check(record["restoredSha256"] == digest, "encoded original integrity")
            if record["kind"] == "Raw":
                check(record["baseId"] == "" and int(record["payloadBytes"]) == length, "Raw representation")
            else:
                base_id = int(record["baseId"])
                check(record["kind"] == "Delta" and base_id < stored_id and by_id[base_id]["kind"] == "Raw", "Raw-only/depth/order")
                check(length >= 12 and int(record["payloadBytes"]) < length, "Delta not strictly smaller")
        check(len({r["storedId"] for r in tries}) == len(tries), "multiple encodings of unique ID")
        for attempt in tries:
            record = by_id[int(attempt["storedId"])]
            base_id = int(attempt["baseId"])
            accepted = int(attempt["deltaBytes"]) < int(attempt["originalBytes"])
            check(base_id < int(record["storedId"]) and by_id[base_id]["kind"] == "Raw", "attempt invalid base")
            check(attempt["originalBytes"] == record["originalBytes"], "attempt length")
            check(attempt["accepted"] == str(accepted).lower(), "gain threshold")
            check(record["kind"] == ("Delta" if accepted else "Raw"), "attempt actual representation")
            check(attempt["reason"] == ("strictly-smaller" if accepted else "no-payload-gain"), "fallback reason")
            check(0 <= int(attempt["querySlot"]) < 3 and 0 <= int(attempt["historicalSlot"]) < 3, "SF slots")
            if accepted:
                check(record["baseId"] == attempt["baseId"] and record["payloadBytes"] == attempt["deltaBytes"], "accepted Delta metadata")
        accepted = [t for t in tries if t["accepted"] == "true"]
        delta_count = sum(r["kind"] == "Delta" for r in records)
        check(delta_count == len(accepted), "accepted Delta count")
        payload_bytes = sum(int(r["payloadBytes"]) for r in records)
        attempted_original = sum(int(t["originalBytes"]) for t in tries)
        attempted_delta = sum(int(t["deltaBytes"]) for t in tries)
        accepted_original = sum(int(t["originalBytes"]) for t in accepted)
        accepted_delta = sum(int(t["deltaBytes"]) for t in accepted)
        values = dict(uniqueChunks=len(records), uniqueOriginalBytes=original_bytes, finalPayloadBytes=payload_bytes,
            rawChunks=len(records) - delta_count, deltaChunks=delta_count, matchedAttempts=len(tries), rawFallbacks=len(tries) - delta_count,
            attemptedOriginalBytes=attempted_original, attemptedDeltaBytes=attempted_delta, acceptedOriginalBytes=accepted_original,
            acceptedDeltaBytes=accepted_delta, maxDeltaDepth=int(delta_count > 0), verifiedOccurrences=len(manifest),
            verifiedBytes=sum(int(m["length"]) for m in manifest), pipelineRuns=1,
            encodeNs=sum(int(t["encodeNs"]) for t in tries), roundtripVerifyNs=sum(int(t["verifyNs"]) for t in tries))
        for key, value in values.items():
            check(int(summary[key]) == value, f"summary {algorithm} {key}")
        ratios = dict(dcr=(original_bytes, payload_bytes), storedResidualRatio=(payload_bytes, original_bytes),
            storedSavingRatio=(original_bytes - payload_bytes, original_bytes), attemptedResidualRatio=(attempted_delta, attempted_original),
            attemptedSavingRatio=(attempted_original - attempted_delta, attempted_original), acceptedResidualRatio=(accepted_delta, accepted_original),
            acceptedSavingRatio=(accepted_original - accepted_delta, accepted_original))
        for key, (numerator, denominator) in ratios.items():
            if denominator == 0:
                check(summary[key] == "", f"undefined ratio {key}")
            else:
                check(math.isclose(float(summary[key]), numerator / denominator, rel_tol=1e-12, abs_tol=1e-15), f"weighted ratio {key}")
    check(len(encoded) == 2 * len(canonical) and len(verification) == 2 * len(manifest), "unexpected algorithm rows")
    return summaries


if __name__ == "__main__":
    for argument in sys.argv[1:]:
        result = verify_delta_results(argument)
        print(f"Verified {argument}: " + "; ".join(f"{r['algorithm']} {r['deltaChunks']} Delta, {r['finalPayloadBytes']} payload bytes, {r['verifiedOccurrences']} restored" for r in result))
