#!/usr/bin/env python3
"""真实 CLI 接入测试：跨文件精确去重、唯一 payload、引用恢复及 unique/all 口径。"""
import collections
import csv
import hashlib
from pathlib import Path
import subprocess
import sys
import tempfile


def check(condition, message):
    if not condition:
        raise AssertionError(message)


def rows(path):
    with path.open(newline="", encoding="utf-8") as stream:
        return list(csv.DictReader(stream))


def metadata(path):
    return dict(line.split("=", 1) for line in path.read_text().splitlines() if "=" in line)


def verify_store(output):
    manifest = rows(output / "manifest.csv")
    references = rows(output / "chunk-references.csv")
    stored = rows(output / "stored-chunks.csv")
    stats = rows(output / "deduplication.csv")[0]
    check(len(references) == len(manifest), "occurrence mapping lost")
    unique = {}
    first_ids = {}
    counts = collections.Counter()
    total_bytes = 0
    short_bytes = 0
    short_chunks = 0
    payloads = {}
    for source, reference in zip(manifest, references):
        contents = (output / source["file"]).read_bytes()
        offset, length = int(source["offset"]), int(source["length"])
        payload = contents[offset:offset + length]
        duplicate = payload in unique
        if not duplicate:
            unique[payload] = len(unique)
            first_ids[payload] = source["chunkId"]
        stored_id = unique[payload]
        check(all(reference[k] == source[k] for k in ("chunkId", "file", "offset", "length")), "source occurrence changed")
        check(int(reference["storedId"]) == stored_id, "equal payloads do not share storedId")
        check(reference["firstChunkId"] == first_ids[payload], "first occurrence identity")
        check(reference["isDuplicate"] == str(duplicate).lower(), "duplicate classification")
        counts[stored_id] += 1
        payloads[stored_id] = payload
        total_bytes += length
        if length < 12:
            short_bytes += length
            short_chunks += 1
    check(len(stored) == len(unique) == int(stats["uniqueChunks"]), "unique payload cardinality")
    for row in stored:
        stored_id = int(row["storedId"])
        payload = payloads[stored_id]
        check(int(row["length"]) == len(payload), "stored payload length")
        check(row["sha1"] == hashlib.sha1(payload).hexdigest(), "stored SHA-1")
        check(int(row["referenceCount"]) == counts[stored_id], "warmup or repeated SF polluted reference count")
        check(row["firstChunkId"] == first_ids[payload], "stored first occurrence")
    unique_bytes = sum(map(len, unique))
    check(int(stats["inputBytes"]) == total_bytes and int(stats["inputChunks"]) == len(manifest), "original counts")
    check(int(stats["uniqueBytes"]) == int(stats["storedPayloadBytes"]) == unique_bytes, "payload stored more than once")
    check(int(stats["duplicateBytes"]) == total_bytes - unique_bytes, "duplicate byte accounting")
    check(int(stats["duplicateChunks"]) == len(manifest) - len(unique), "duplicate count accounting")
    check(stats["scope"] == "process-memory-payload", "storage scope")
    mode = metadata(output / "run.txt")["sfInput"]
    selected = list(unique) if mode == "unique" else [payloads[int(r["storedId"])] for r in references]
    selected_bytes = sum(map(len, selected))
    selected_short = [p for p in selected if len(p) < 12]
    processed = sum(len(p) for p in selected if len(p) >= 12)
    for row in rows(output / "measurements.csv"):
        check(row["sfInput"] == mode, "measurement mode missing")
        check(int(row["inputBytes"]) == total_bytes and int(row["excludedBytes"]) == short_bytes, "original byte fields changed")
        check(int(row["excludedChunks"]) == short_chunks, "original short count changed")
        check(int(row["sfInputBytes"]) == selected_bytes and int(row["sfInputChunks"]) == len(selected), "selected input counts")
        check(int(row["sfExcludedBytes"]) == sum(map(len, selected_short)), "selected short bytes")
        check(int(row["sfExcludedChunks"]) == len(selected_short), "selected short chunks")
        check(int(row["processedBytes"]) == processed * 3, "SF processedBytes uses wrong set")
    diagnostics = rows(output / "diagnostics.csv")
    check(len(diagnostics) == sum(len(p) >= 12 for p in selected) * 2, "diagnostics selection differs from SF selection")
    if mode == "unique":
        allowed = {first_ids[p] for p in unique if len(p) >= 12}
        check(all(r["chunkId"] in allowed for r in diagnostics), "duplicate reached unique diagnostics")
    meta = metadata(output / "run.txt")
    check(meta["sourcePayloadRetained"] == "false" and meta["storageEnabled"] == "true", "store integration metadata")
    check(meta["inputDeduplicated"] == str(mode == "unique").lower(), "SF input dedup metadata")
    return stats


def main():
    binary = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="finesse-dedup-") as directory:
        root = Path(directory)
        alpha = b"0123456789abcdef\0" * 64
        fixtures = {"00-alpha.bin": alpha, "01-alpha-copy.bin": alpha,
            "02-different.bin": alpha[:-1] + b"\xff", "03-short.bin": b"xy", "04-short-copy.bin": b"xy",
            "05-empty.bin": b"", "06-zero.bin": bytes(4096), "07-zero-copy.bin": bytes(4096)}
        for filename, data in fixtures.items():
            (root / filename).write_bytes(data)
        (root / "files.txt").write_text("\n".join(reversed(sorted(fixtures))) + "\n")
        base = dict(profileId="finesse-gear-v1", inputMode="raw", inputPath="files.txt", datasetKind="synthetic",
            datasetDescription="dedup CLI fixture; fixed binary/zero data; 8 files, seed not used", runId="dedup",
            expectedSize="8192", minSize="2048", maxSize="65536", ncLevel="0", warmupRuns="1",
            measurementRuns="5", iterations="3", diagnostics="true", sfInput="unique")

        def run(name, **updates):
            config = base | dict(outputDirectory=name) | updates
            path = root / (name + ".conf")
            path.write_text("".join(f"{key}={value}\n" for key, value in config.items()))
            result = subprocess.run([str(binary), "--config", str(path)], cwd=root.parent, capture_output=True, text=True)
            check(result.returncode == 0, f"{name}: {result.stderr}")
            return root / name

        unique = run("unique")
        all_input = run("all", sfInput="all")
        stats = verify_store(unique)
        verify_store(all_input)
        check(int(stats["inputChunks"]) == 7 and int(stats["uniqueChunks"]) == 4, "fixture not deduplicated as expected")
        check(int(stats["duplicateChunks"]) == 3, "cross-file duplicates not detected")
        check(len(rows(unique / "source-checksums.csv")) == 8, "empty file metadata lost")
        for mode, original in (("unique", unique), ("all", all_input)):
            replay = run("replay-" + mode, inputMode="manifest", inputPath=mode + "/manifest.csv", sfInput=mode)
            verify_store(replay)
            before = [(r["algorithm"], r["outputDigest"]) for r in rows(original / "measurements.csv")]
            after = [(r["algorithm"], r["outputDigest"]) for r in rows(replay / "measurements.csv")]
            check(before == after, "store-backed replay changed signatures")
            check(rows(original / "stored-chunks.csv") == rows(replay / "stored-chunks.csv"), "replay changed unique pool")
    print("Dedup CLI checks passed: one copy per content, references restore source chunks, unique/all/replay agree.")


if __name__ == "__main__":
    main()
