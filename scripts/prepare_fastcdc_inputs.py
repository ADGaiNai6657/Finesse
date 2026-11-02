#!/usr/bin/env python3
"""D-20：只读枚举 FastCDC 四组数据，导出相对路径列表，不覆盖已有清单。"""
import argparse

from dataset_inputs import prepare_inputs, project_path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dataset-root", default="../FastCDC/Dataset",
                        help="relative to Finesse project root, not current working directory")
    parser.add_argument("--output-directory", default="configs/inputs-fastcdc",
                        help="relative to Finesse project root; must not exist")
    args = parser.parse_args()
    prepare_inputs(project_path(args.dataset_root), project_path(args.output_directory),
                   ((1, False), (2, True), (3, False), (4, False)), "fastcdc")


if __name__ == "__main__":
    main()
