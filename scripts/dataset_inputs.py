"""数据目录只读枚举和相对路径清单导出，供数据准备脚本共用。"""
import csv
import hashlib
import io
import os
from pathlib import Path
import stat

PROJECT_ROOT = Path(__file__).resolve().parents[1]


#按项目根目录解析准备脚本的路径参数
#输入：value：相对项目根目录或绝对的路径。
#输出：返回 resolve 后的 Path，不以启动 cwd 为相对基准。
def project_path(value):
    path = Path(value)
    return (path if path.is_absolute() else PROJECT_ROOT / path).resolve()


#把文件路径转换为相对目录的可移植文本
#输入：path：目标路径；directory：相对基准目录。
#输出：返回使用正斜线的相对路径字符串，不读写文件。
def relative(path, directory):
    return Path(os.path.relpath(path, directory)).as_posix()


#只读枚举数据文件并单独记录 macOS 元数据
#输入：directory：数据目录 Path；recursive：是否递归子目录。
#输出：返回排序后的 (files, excluded)，每项为路径和字节数；目录缺失、符号链接或非普通项抛异常。
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


#将二维记录编码成 CSV 文本
#输入：rows：按输出顺序排列的字段行。
#输出：返回带必要转义和换行的字符串，不写文件。
def encode_csv(rows):
    text = io.StringIO(newline="")
    csv.writer(text, lineterminator="\n").writerows(rows)
    return text.getvalue()


#为各组数据导出相对路径清单和枚举摘要
#输入：dataset_root：数据根目录；output：必须不存在的输出目录；groups：(组号,是否递归)序列；prefix：文件名前缀。
#输出：返回 None；写各组 files.txt、inventory.csv 和 excluded-files.csv 并打印汇总，原数据只读且已有目录拒绝覆盖。
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
