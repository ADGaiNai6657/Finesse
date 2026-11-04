#!/usr/bin/env python3
"""Real CLI: frozen config, Raw/Delta and exact references, replay and failure states."""
from pathlib import Path
import hashlib
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from verify_delta_results import check, rows, metadata, verify_delta_results


def main():
    binary = Path(sys.argv[1]).resolve()
    repo = Path(__file__).resolve().parents[1]
    with tempfile.TemporaryDirectory(prefix="finesse-delta-") as directory:
        root = Path(directory)
        alpha = bytes(range(256)) * 32
        changed = bytearray(alpha); changed[100] ^= 1
        fixtures = {"00-base.bin": alpha, "01-a.bin": changed, "02-duplicate.bin": changed,
            "03-short.bin": b"short", "04-short-copy.bin": b"short", "05-empty.bin": b""}
        for name, content in fixtures.items():
            (root / name).write_bytes(content)
        (root / "files.txt").write_text("\n".join(reversed(sorted(fixtures))) + "\n")
        input_values = dict(profileId="finesse-gear-v1", inputMode="raw", inputPath="files.txt", datasetKind="synthetic",
            datasetDescription="Delta CLI binary edits; no random seed", outputDirectory="DO-NOT-USE", runId="inner",
            expectedSize="8192", minSize="2048", maxSize="65536", ncLevel="0", warmupRuns="1", measurementRuns="5",
            iterations="99", diagnostics="true", sfInput="unique")
        def save_input(**updates):
            values = input_values | updates
            (root / "input.conf").write_text("".join(f"{k}={v}\n" for k, v in values.items()))
        save_input()
        template = dict(line.split("=", 1) for line in (repo / "configs/fastcdc-dataset-4-delta.conf").read_text().splitlines() if line and not line.startswith("#"))
        template |= dict(inputConfig="input.conf", runId="delta-test")
        def invoke(name, values=None, suffix="", success=True):
            values = template | dict(outputDirectory=name) if values is None else values
            config = root / (name + ".conf")
            config.write_text("".join(f"{k}={v}\n" for k, v in values.items()) + suffix)
            completed = subprocess.run([str(binary), "--delta-config", str(config)], cwd=root.parent, capture_output=True, text=True)
            check((completed.returncode == 0) == success, f"{name}: {completed.stdout} {completed.stderr}")
            return root / name
        output = invoke("raw")
        summary = verify_delta_results(output)
        check(all(int(s["deltaChunks"]) > 0 and int(s["verifiedOccurrences"]) == 5 for s in summary), "fixture missing Delta/duplicate/short")
        check(len(rows(output / "source-checksums.csv")) == 6, "empty source missing")
        check(not (root / "DO-NOT-USE").exists() and not (output / "measurements.csv").exists(), "ran SF microbenchmark/used inner output")
        before = (output / "run.txt").read_bytes()
        invoke("raw", success=False)
        check((output / "run.txt").read_bytes() == before, "overwrote old results")
        save_input(inputMode="manifest", inputPath="raw/manifest.csv")
        replay = invoke("replay")
        verify_delta_results(replay)
        cols = ("algorithm", "storedId", "firstChunkId", "kind", "baseId", "originalBytes", "payloadBytes", "restoredSha256")
        check([[r[c] for c in cols] for r in rows(output / "encoded-chunks.csv")] == [[r[c] for c in cols] for r in rows(replay / "encoded-chunks.csv")], "replay representation changed")
        save_input()
        for key in template:
            missing = template | dict(outputDirectory="missing-" + key)
            del missing[key]
            invoke("missing-" + key, missing, success=False)
        invoke("unknown", suffix="unknown=1\n", success=False)
        invoke("duplicate", suffix="codec=xdelta3\n", success=False)
        for key in ("pipelineProfile", "algorithms", "codec", "codecRevision", "compressionLevel", "secondaryCompression", "checksum",
                    "basePolicy", "maxDeltaDepth", "gainPolicy", "storage", "verification", "pipelineRuns"):
            invoke("bad-" + key, template | {key: "unapproved", "outputDirectory": "bad-" + key}, success=False)
        save_input(sfInput="all")
        invoke("all-rejected", success=False)
        save_input(inputMode="manifest", inputPath="raw/manifest.csv")
        (output / "checksums.csv").write_text("file,sha256\nmanifest.csv," + "0" * 64 + "\n")
        failed = invoke("failed", success=False)
        check(metadata(failed / "run.txt")["status"] == "failed" and not (failed / "compression-summary.csv").exists(), "failure masquerades as success")
        # Empty raw input is valid for storage quality, with undefined ratios left blank.
        (root / "empty.files.txt").write_text("05-empty.bin\n")
        save_input(inputPath="empty.files.txt")
        empty = invoke("empty")
        verify_delta_results(empty)
        check(all(s["dcr"] == "" and s["verifiedOccurrences"] == "0" for s in rows(empty / "compression-summary.csv")), "empty denominator")
        # Metadata audit must detect corrupted restoration evidence.
        original = (empty / "compression-summary.csv").read_text()
        (empty / "compression-summary.csv").write_text(original + "corrupt")
        try:
            verify_delta_results(empty)
        except AssertionError:
            pass
        else:
            raise AssertionError("corrupted output accepted")
    print("Delta CLI/config/replay/audit tests passed")


if __name__ == "__main__":
    main()
