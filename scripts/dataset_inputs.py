"""数据目录只读枚举和相对路径清单导出，供数据准备脚本共用。"""
import csv
import hashlib
import io
import os
from pathlib import Path
import stat

PROJECT_ROOT = Path(__file__).resolve().parents[1]


def project_path(value):
    path = Path(value)
    return (path if path.is_absolute() else PROJECT_ROOT / path).resolve()


def relative(path, directory):
    return Path(os.path.relpath(path, directory)).as_posix()


def collect(directory, recursive):
    files = []
    excluded = []
    if not directory.is_dir():
        raise ValueError(f"dataset directory missing: {directory}")
    for path in sorted(directory.iterdir()):
        mode = path.lstat().st_mode
        if stat.S_ISLNK(mode):
            raise ValueError(f"unexpected symbolic link in dataset: {path}")
        if stat.S_ISDIR(mode):
            if recursive:
                children, skipped = collect(path, True)
                files.extend(children)
                excluded.extend(skipped)
        elif stat.S_ISREG(mode):
            record = (path, path.stat().st_size)
            if path.name == ".DS_Store" or path.name.startswith("._"):
                excluded.append(record)
            else:
                files.append(record)
        else:
            raise ValueError(f"unexpected non-regular dataset entry: {path}")
    return sorted(files), sorted(excluded)


def encode_csv(rows):
    text = io.StringIO(newline="")
    csv.writer(text, lineterminator="\n").writerows(rows)
    return text.getvalue()


def prepare_inputs(dataset_root, output, groups, prefix):
    inventory = [["dataset", "fileCount", "inputBytes", "excludedFiles", "excludedBytes", "filesListSha256"]]
    excluded_rows = [["dataset", "file", "bytes", "reason"]]
    lists = {}
    for number, recursive in groups:
        name = f"DataSet_{number}"
        files, excluded = collect(dataset_root / name, recursive)
        if not files:
            raise ValueError(f"no dataset files: {name}")
        lines = [relative(path, output) for path, _ in files]
        if any("\n" in line or "\r" in line for line in lines):
            raise ValueError("raw file lists cannot represent newline characters in filenames")
        text = "\n".join(lines) + "\n"
        lists[f"{prefix}-dataset-{number}.files.txt"] = text
        inventory.append([name, len(files), sum(size for _, size in files), len(excluded),
                          sum(size for _, size in excluded), hashlib.sha256(text.encode()).hexdigest()])
        excluded_rows.extend([name, relative(path, output), size, "macOS metadata"] for path, size in excluded)
    # 先枚举、校验所有组，再创建新目录；不重写已经用于实验的清单。
    output.mkdir(parents=True, exist_ok=False)
    for filename, text in lists.items():
        (output / filename).write_text(text, encoding="utf-8")
    (output / "inventory.csv").write_text(encode_csv(inventory), encoding="utf-8")
    (output / "excluded-files.csv").write_text(encode_csv(excluded_rows), encoding="utf-8")
    for row in inventory[1:]:
        print(f"{row[0]}: files={row[1]}, bytes={row[2]}, excludedFiles={row[3]}, excludedBytes={row[4]}")
    print(f"Lists: {relative(output, PROJECT_ROOT)}")
