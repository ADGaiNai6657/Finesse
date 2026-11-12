#!/usr/bin/env python3
"""D-19 历史配置：只读枚举 TTTD-S 两组数据，不覆盖已有清单。"""
import argparse

from dataset_inputs import prepare_inputs, project_path


#为历史 TTTD-S 两组方案准备相对路径清单
#输入：无显式参数，通过命令行读取 dataset-root 和 output-directory，默认值相对项目根目录。
#输出：返回 None；写两组清单和统计，第二组递归，拒绝覆盖已有目录，原数据只读。
def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dataset-root", default="../TTTD-S_Experiments/DataSet",
                        help="relative to Finesse project root, not current working directory")
    parser.add_argument("--output-directory", default="configs/inputs",
                        help="relative to Finesse project root; must not exist")
    args = parser.parse_args()
    prepare_inputs(project_path(args.dataset_root), project_path(args.output_directory),
                   ((1, False), (2, True)), "tttd-s")


if __name__ == "__main__":
    main()
