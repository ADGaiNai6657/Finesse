#!/usr/bin/env python3
"""D-17 验收样例；只使用标准库。已有目录拒绝覆盖。"""
import argparse
from pathlib import Path


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("directory", type=Path)
    args = parser.parse_args()
    args.directory.mkdir(parents=True, exist_ok=False)
    size = 256 * 1024
    state = 0x13579bdf
    data = bytearray()
    for _ in range(size):
        state ^= (state << 13) & 0xffffffff
        state ^= state >> 17
        state ^= (state << 5) & 0xffffffff
        data.append(state & 255)
    original = bytes(data)
    files = {"00-zero.bin": bytes(size), "01-ff.bin": b"\xff" * size,
             "02-sequence.bin": bytes(range(256)) * (size // 256), "03-random.bin": original,
             "04-repeated.bin": original}
    replaced = bytearray(original)
    replaced[size // 2:size // 2 + 256] = b"\xa5" * 256
    files["05-replaced.bin"] = bytes(replaced)
    for label, position in (("head", 0), ("middle", size // 2), ("tail", size)):
        files[f"insert-{label}.bin"] = original[:position] + b"Finesse-Gear-v1!" + original[position:]
        deletion = min(position, size - 17)
        files[f"delete-{label}.bin"] = original[:deletion] + original[deletion + 17:]
    files["shared-short.bin"] = original[:size // 2]
    files["shared-long.bin"] = original + original[:size // 2]
    for n in range(15):
        files[f"short-{n:02d}.bin"] = original[:n]
    description = ["datasetKind=synthetic", "generator=generate_synthetic.py validation-v1",
        "prng=xorshift32; x^=x<<13; x^=x>>17; x^=x<<5; uint32 state, emit low byte",
        "seed=0x13579bdf", f"baseBytes={size}", "replacement=offset 131072,length 256,value 0xa5",
        "insert=head 0/middle 131072/tail 262144, ASCII Finesse-Gear-v1! (16 bytes)",
        "delete=head 0/middle 131072/tail 262127,length 17",
        "shared=first 131072 bytes / original plus first 131072 bytes",
        "short=original prefixes of lengths 0..14", "deduplicated=false"]
    for name, contents in sorted(files.items()):
        (args.directory / name).write_bytes(contents)
        description.append(f"file={name};bytes={len(contents)}")
    description.append(f"totalBytes={sum(map(len, files.values()))}")
    (args.directory / "dataset.txt").write_text("\n".join(description) + "\n", encoding="utf-8")
    (args.directory / "files.txt").write_text("\n".join(sorted(files)) + "\n", encoding="utf-8")
    print(args.directory / "dataset.txt")


if __name__ == "__main__":
    main()
