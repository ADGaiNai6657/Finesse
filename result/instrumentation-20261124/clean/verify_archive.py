"""Verify the existing source archive against the source bytes consumed by this rerun."""
import csv
import hashlib
from pathlib import Path
import tarfile


def source_inventory(root):
    """Map archive member names to file length and SHA-256 from the new runs."""
    expected = {}
    for dataset in range(1, 5):
        run = root / "raw" / f"ds{dataset}-delta-benchmark"
        with (run / "source-checksums.csv").open(newline="") as stream:
            for row in csv.DictReader(stream):
                path = (run / row["file"]).resolve()
                group = f"DataSet_{dataset}"
                name = Path(*path.parts[path.parts.index(group):]).as_posix()
                assert name not in expected, name
                expected[name] = int(row["bytes"]), row["sha256"]
    return expected


def main():
    """Read the archive without extracting files, then match every entry."""
    root = Path(__file__).resolve().parents[1]
    expected = source_inventory(root)
    file_count = len(expected)
    byte_count = sum(length for length, _ in expected.values())
    with tarfile.open(root.parent / "legacy-20261123/raw/source-inputs.tar", "r:") as archive:
        for member in archive:
            assert member.isfile(), member.name
            length, digest = expected.pop(member.name)
            with archive.extractfile(member) as stream:
                assert member.size == length and hashlib.file_digest(stream, "sha256").hexdigest() == digest, member.name
    assert not expected, expected
    print(f"Verified every archived input against the new raw source checksums: {file_count} files, {byte_count} bytes.")


if __name__ == "__main__":
    main()
