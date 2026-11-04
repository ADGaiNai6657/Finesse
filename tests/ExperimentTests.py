#!/usr/bin/env python3
"""验证完整 CLI、清单重放、错误契约与从原始测量重算汇总。"""
import csv
import hashlib
import io
import math
from pathlib import Path
import statistics
import subprocess
import sys
import tempfile


def check(condition, message):
    if not condition:
        raise AssertionError(message)


def rows(path):
    with path.open(newline="", encoding="utf-8") as stream:
        return list(csv.DictReader(stream))


def csv_bytes(data):
    stream = io.StringIO(newline="")
    csv.writer(stream, lineterminator="\n").writerows(data)
    return stream.getvalue().encode()


def main():
    binary = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="finesse-test-") as temporary:
        root = Path(temporary)
        source = root / 'data, "中文".bin'
        state = 0x13579bdf
        data = bytearray()
        for _ in range(131089):
            state ^= (state << 13) & 0xffffffff
            state ^= state >> 17
            state ^= (state << 5) & 0xffffffff
            data.append(state & 255)
        source.write_bytes(data)
        short = root / "short.bin"
        short.write_bytes(b"abc")
        (root / "empty.bin").write_bytes(b"")
        (root / "files.txt").write_text(short.name + "\n" + source.name + "\nempty.bin\n", encoding="utf-8")
        base = dict(profileId="finesse-gear-v1", inputMode="raw", inputPath="files.txt",
            datasetKind="synthetic", datasetDescription="CLI correctness fixture; xorshift32 seed=0x13579bdf bytes=131089 plus 3 byte short file",
            outputDirectory="raw", runId="integration", expectedSize="8192", minSize="2048", maxSize="65536",
            ncLevel="0", warmupRuns="1", measurementRuns="5", iterations="2", diagnostics="true", sfInput="all")

        def run(name, updates=None, success=True, suffix=""):
            values = base | (updates or {})
            path = root / (name + ".conf")
            path.write_text("".join(f"{k}={v}\n" for k, v in values.items()) + suffix, encoding="utf-8")
            result = subprocess.run([str(binary), "--config", str(path)], capture_output=True, text=True)
            check((result.returncode == 0) == success, f"{name}: {result.stdout} {result.stderr}")
            return result

        run("raw")
        output = root / "raw"
        check("status=ok\n" in (output / "run.txt").read_text(), "successful run status")
        raw = rows(output / "measurements.csv")
        check(len(raw) == 10, "five measurements per algorithm")
        for algorithm in ("Finesse-Gear", "NTransform-Gear"):
            records = [r for r in raw if r["algorithm"] == algorithm]
            check(len({r["outputDigest"] for r in records}) == 1, "determinism")
            for r in records:
                check(int(r["processedBytes"]) == len(data) * 2, "same effective bytes")
                check(int(r["excludedBytes"]) == 3 and int(r["excludedChunks"]) == 1, "short input accounting")
                first = "Finesse-Gear" if int(r["repetition"]) % 2 else "NTransform-Gear"
                check(int(r["order"]) == (1 if algorithm == first else 2), "alternating algorithm order")
        summary = rows(output / "summary.csv")
        means = {}
        for s in summary:
            records = [r for r in raw if r["algorithm"] == s["algorithm"]]
            seconds = [int(r["elapsedNs"]) / 1e9 for r in records]
            speeds = [int(r["processedBytes"]) / t for r, t in zip(records, seconds)]
            means[s["algorithm"]] = statistics.mean(seconds)
            for key, expected in (("meanSeconds", statistics.mean(seconds)), ("stddevSeconds", statistics.pstdev(seconds)),
                ("meanBytesPerSecond", statistics.mean(speeds)), ("stddevBytesPerSecond", statistics.pstdev(speeds)),
                ("meanMBPerSecond", statistics.mean(speeds) / 1e6)):
                check(math.isclose(float(s[key]), expected, rel_tol=1e-10, abs_tol=1e-15), "summary mismatch " + key)
        ratio = means["NTransform-Gear"] / means["Finesse-Gear"]
        check(all(math.isclose(float(s["meanBaselineSecondsOverMeanFinesseSeconds"]), ratio, rel_tol=1e-12) for s in summary), "speed ratio")
        for row in rows(output / "checksums.csv"):
            check(hashlib.sha256((output / row["file"]).read_bytes()).hexdigest() == row["sha256"], "export checksum")
        all_sources = rows(output / "source-checksums.csv")
        check(len(all_sources) == 3 and any(r["bytes"] == "0" for r in all_sources), "empty source provenance D-18")
        for i, row in enumerate(rows(output / "parameters.csv")):
            digest = hashlib.sha256(f"Finesse-Gear/NTransform/v1/{i}".encode()).digest()
            check(int(row["m_i"], 16) == int.from_bytes(digest[:4], "little") | 1, "D-15 multiplier")
            check(int(row["a_i"], 16) == int.from_bytes(digest[4:8], "little"), "D-15 addend")
        manifest = rows(output / "manifest.csv")
        file_order = [r["file"] for r in manifest]
        check(file_order == sorted(file_order), "sorted files")
        run("replay", dict(inputMode="manifest", inputPath="raw/manifest.csv", outputDirectory="replay"))
        replay = rows(root / "replay" / "measurements.csv")
        check([(r["algorithm"], r["outputDigest"]) for r in raw] == [(r["algorithm"], r["outputDigest"]) for r in replay], "manifest replay digest")
        run("overwrite", success=False)
        check("status=ok\n" in (output / "run.txt").read_text(), "overwrite must preserve completed run")
        run("unknown", {"outputDirectory": "unknown"}, success=False, suffix="unknown=x\n")
        run("duplicate", {"outputDirectory": "duplicate"}, success=False, suffix="iterations=2\n")
        missing = base.copy()
        missing.pop("iterations")
        path = root / "missing.conf"
        path.write_text("".join(f"{k}={v}\n" for k, v in missing.items()))
        check(subprocess.run([str(binary), "--config", str(path)], capture_output=True).returncode != 0, "missing key")
        run("zero-min", {"minSize": "0", "outputDirectory": "zero-min"}, success=False)
        run("zero-iterations", {"iterations": "0", "outputDirectory": "zero-iterations"}, success=False)
        run("invalid-sf-input", {"sfInput": "auto", "outputDirectory": "invalid-sf-input"}, success=False)
        run("overflow", {"iterations": str(2**64 - 1), "outputDirectory": "overflow"}, success=False)
        check("status=failed\n" in (root / "overflow/run.txt").read_text(), "overflow failed status")
        (root / "onlyshort.txt").write_text("short.bin\n")
        run("empty-effective", dict(inputPath="onlyshort.txt", outputDirectory="empty-effective"), success=False)
        check(not (root / "empty-effective/summary.csv").exists(), "no fake zero-byte summary")
        check("status=failed\n" in (root / "empty-effective/run.txt").read_text(), "failed status")
        run("missing-source", dict(inputPath="does-not-exist.txt", outputDirectory="missing-source"), success=False)
        check("status=failed\n" in (root / "missing-source/run.txt").read_text(), "I/O failed status")
        # 每个损坏清单重算清单本身摘要，确保测试覆盖范围/ID 校验而不只是摘要检查。
        bad_dir = root / "bad"
        bad_dir.mkdir()
        original_rows = list(csv.reader((output / "manifest.csv").open(newline="")))

        def bad_manifest(name, alter, checksum_alter=None):
            modified = [row.copy() for row in original_rows]
            alter(modified)
            contents = csv_bytes(modified)
            (bad_dir / "manifest.csv").write_bytes(contents)
            checksums = [["file", "sha256"], ["manifest.csv", hashlib.sha256(contents).hexdigest()]]
            for p in (source, short):
                checksums.append(["../" + p.name, hashlib.sha256(p.read_bytes()).hexdigest()])
            if checksum_alter:
                checksum_alter(checksums)
            (bad_dir / "checksums.csv").write_bytes(csv_bytes(checksums))
            run(name, dict(inputMode="manifest", inputPath="bad/manifest.csv", outputDirectory=name), success=False)

        bad_manifest("bad-range", lambda r: r[1].__setitem__(3, str(2**64 - 1)))
        bad_manifest("bad-id", lambda r: r[2].__setitem__(0, r[1][0]))
        bad_manifest("bad-checksum", lambda r: None, lambda r: r[1].__setitem__(1, "0" * 64))
        bad_manifest("extra-checksum", lambda r: None, lambda r: r.append(["../files.txt", hashlib.sha256((root / "files.txt").read_bytes()).hexdigest()]))
        original_source = source.read_bytes()
        source.write_bytes(b"x" + original_source[1:])
        run("changed-source", dict(inputMode="manifest", inputPath="raw/manifest.csv", outputDirectory="changed-source"), success=False)
        source.write_bytes(original_source)
        # 重复内容保留不同 ID，并支持标准 CSV 中含换行的文件路径。
        newline_file = root / "newline\nfile.bin"
        newline_file.write_bytes(original_source[:64])
        content = csv_bytes([["chunkId", "file", "offset", "length"], ["500", "../" + newline_file.name, "0", "64"],
                             ["2", "../" + newline_file.name, "0", "64"]])
        (bad_dir / "manifest.csv").write_bytes(content)
        (bad_dir / "checksums.csv").write_bytes(csv_bytes([["file", "sha256"], ["manifest.csv", hashlib.sha256(content).hexdigest()],
            ["../" + newline_file.name, hashlib.sha256(newline_file.read_bytes()).hexdigest()]]))
        run("repeated-content", dict(inputMode="manifest", inputPath="bad/manifest.csv", outputDirectory="repeated-content"))
        diagnostics = rows(root / "repeated-content/diagnostics.csv")
        for algorithm in ("Finesse-Gear", "NTransform-Gear"):
            matches = [r for r in diagnostics if r["algorithm"] == algorithm]
            check(matches[0]["status"] == "unmatched" and matches[1]["candidateId"] == "500", "query before insert, insertion order")
    print("Experiment checks passed (T-10/T-12, replay, CSV, config, failures).")


if __name__ == "__main__":
    main()
