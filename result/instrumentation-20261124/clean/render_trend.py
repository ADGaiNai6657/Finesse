"""Generate plain Markdown and HTML trend reports from the audited clean tables."""
import csv
import html
from pathlib import Path
import statistics


ROOT = Path(__file__).resolve().parents[1]
FINESSE = "Finesse-Gear"
BASELINE = "NTransform-Gear"


def read(name):
    """Read one cleaned table, keeping exact decimal strings."""
    with (ROOT / "clean" / name).open(newline="", encoding="utf-8") as stream:
        return list(csv.DictReader(stream))


class Report:
    """Build Markdown and HTML from the same text, tables and sources."""

    def __init__(self):
        self.markdown = []
        self.htm = []

    def heading(self, level, text):
        self.markdown.append(f"{'#' * level} {text}")
        self.htm.append(f"<h{level}>{html.escape(text)}</h{level}>")

    def paragraph(self, text):
        self.markdown.append(text)
        self.htm.append(f"<p>{html.escape(text)}</p>")

    def table(self, headers, records):
        rows = list(records)
        self.markdown.append("\n".join("| " + " | ".join(row) + " |"
                                       for row in [headers, ["---"] * len(headers), *rows]))
        head = "".join(f"<th>{html.escape(value)}</th>" for value in headers)
        body = "".join("<tr>" + "".join(f"<td>{html.escape(value)}</td>" for value in row)
                       + "</tr>" for row in rows)
        self.htm.append(f'<table border="1"><thead><tr>{head}</tr></thead><tbody>{body}</tbody></table>')

    def links(self, *sources):
        for _, link in sources:
            if not (ROOT / link).is_file():
                raise FileNotFoundError(link)
        self.markdown.append(" · ".join(f"[{label}]({url})" for label, url in sources))
        self.htm.append("<p>" + " · ".join(f'<a href="{html.escape(url)}">{html.escape(label)}</a>'
                                            for label, url in sources) + "</p>")

    def write(self):
        (ROOT / "trend-analysis.md").write_text("\n\n".join(self.markdown) + "\n", encoding="utf-8")
        title = '<meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">'
        title += "<title>Finesse 新版趋势分析</title>"
        (ROOT / "trend-analysis.html").write_text(
            f'<!doctype html><html lang="zh-CN"><head>{title}</head><body>\n'
            + "\n".join(self.htm) + "\n</body></html>\n", encoding="utf-8")


def collect():
    """Join throughput, compression, locality and selected benchmark phases by dataset."""
    quality = {(r["dataset"], r["algorithm"]): r for r in read("table3-compression.csv")}
    locality = {r["dataset"]: r for r in read("table1-locality.csv")}
    timing = {(r["dataset"], r["algorithm"], r["stage_name"]): r["total_time_ms"]
              for r in read("stage-timing.csv") if r["entry"] == "delta" and r["mode"] == "benchmark"}
    rows = []
    for trend in read("trend-comparison.csv"):
        dataset = trend["dataset"]
        row = dict(trend, cdc_ms=timing[(dataset, "FastCDC", "cdc_total")])
        for label, algorithm in (("finesse", FINESSE), ("ntransform", BASELINE)):
            source = quality[(dataset, algorithm)]
            for key in ("matchedAttempts", "attemptedSavingRatio", "dcr", "finalPayloadBytes"):
                row[f"{label}_{key}"] = source[key]
            for stage in ("encoded_pool_build", "feature_extraction", "delta_encode",
                          "roundtrip_verify", "chunk_recovery", "file_reassembly", "file_sha256_verify"):
                row[f"{label}_{stage}_ms"] = timing.get((dataset, algorithm, stage), "")
        for key in ("candidatePairs", "meanIdentical", "meanEqualFeature"):
            row[key] = locality[dataset][key]
        rows.append(row)
    return rows


def decimal(value, places=2):
    """Show an unexecuted stage as a dash and retain measured zeroes."""
    return f"{float(value):.{places}f}" if value != "" else "—"


def pair(row, field, formatter=decimal):
    """Display the Finesse and NTransform values in a consistent order."""
    return " / ".join(formatter(row[f"{label}_{field}"]) for label in ("finesse", "ntransform"))


def main():
    rows = collect()
    workloads = [r["workload"] for r in read("table2-workloads.csv")]
    with (ROOT / "clean/trend-analysis.csv").open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)

    report = Report()
    report.heading(1, "Finesse 新版实验趋势分析 · 2026-11-24")
    report.paragraph("部分复现了论文趋势。Finesse-Gear 在四组数据上计算 SF 均快约三倍，"
                     "单算法内存管线吞吐也全部提高。主要偏差仍是整体 Delta 压缩比，"
                     "集中于展开源码和未压缩 tar。新版 Benchmark 和 Profiling 的算法输出一致，"
                     "两种模式的压缩表示与逐块恢复证据也与旧实验一致。")
    report.links(("趋势复算 CSV", "clean/trend-analysis.csv"), ("完整实验报告", "report.html"),
                 ("模式与历史复核", "clean/reproducibility.csv"),
                 ("旧趋势分析", "../legacy-20261123/trend-analysis.html"))

    report.heading(2, "SF 速度和单算法内存吞吐")
    report.table(["数据集", "负载", "SF 速度倍数", "管线吞吐增幅", "DCR 相对变化"], [
        [r["dataset"], workload, f'{float(r["sf_speedup"]):.2f}×',
         f'{float(r["pipeline_speedup"]) - 1:+.1%}',
         f'{float(r["dcr_change_pct"]):+.3f}%' if i == 0 else f'{float(r["dcr_change_pct"]):+.2f}%']
        for i, (r, workload) in enumerate(zip(rows, workloads))
    ])
    mean = statistics.mean(float(r["sf_speedup"]) for r in rows)
    report.paragraph(f"四组 SF 速度倍数的等权平均为 {mean:.2f}×，接近论文图 4 的 3.2×（E5）"
                     "和 3.5×（i7）。本机各算法均用五轮速度的算术平均计算倍数。"
                     "单算法内存管线从加载开始计至编码池构建完成，和论文图 5 的 Destor 系统口径不同。"
                     "本次只有第 3 组达到论文所报 41%–85% 的吞吐增幅区间。")
    report.links(("80 条正式测速记录", "clean/formal-rounds.csv"), ("论文图 4", "paper/figure4.png"),
                 ("论文图 5", "paper/figure5.png"))

    report.heading(2, "DCR 差额集中在两组的候选覆盖率")
    report.paragraph("每格按 Finesse-Gear / NTransform-Gear 排列。匹配覆盖率的分母是去重后的唯一原文字节。"
                     "已匹配节省率的分母是匹配目标原文字节，没有匹配时显示为不适用。"
                     "DCR 为唯一原文字节除以最终 Raw 与 Delta 的合计 payload。")
    report.table(["数据集", "DCR", "匹配块数", "匹配覆盖率", "已匹配节省率"], [
        [r["dataset"], pair(r, "dcr", lambda s: decimal(s, 5)),
         pair(r, "matchedAttempts", lambda s: f"{int(s):,}"),
         f'{float(r["finesse_covered_pct"]):.2f}% / {float(r["ntransform_covered_pct"]):.2f}%',
         pair(r, "attemptedSavingRatio", lambda s: f"{float(s):.2%}" if s else "—")]
        for r in rows
    ])
    gap = sum(int(r["payload_gap_bytes"]) for r in rows[1:3])
    share = gap / sum(int(r["payload_gap_bytes"]) for r in rows)
    report.paragraph(f"第 2、3 组中，Finesse 命中的块节省率分别高出 2.02 和 2.46 个百分点，"
                     f"但匹配覆盖率少了 13.23 和 13.90 个百分点，导致两组合计多存 {gap / 1e6:.1f} MB，"
                     f"占四组字节差额的 {share:.1%}。论文六组的 DCR 相对变化为 −3.21% 至 +7.36%。"
                     "本机第 2、3 组的降幅分别为 16.77% 和 16.50%。"
                     "论文 DCE 的公式和解释方向存在歧义，本表使用明确的字节节省率。")
    report.links(("本机压缩表", "clean/table3-compression.csv"), ("论文表 3", "paper/table3.png"))

    report.heading(2, "对应子块局部性弱于论文样本")
    report.table(["数据集", "候选对数", "平均相同内容子块数", "平均相同特征子块数"], [
        [r["dataset"], f'{int(r["candidatePairs"]):,}', decimal(r["meanIdentical"]), decimal(r["meanEqualFeature"])]
        for r in rows
    ])
    report.paragraph("第 2–4 组中，特征相同的对应子块均多于内容完全相同的子块，方向与论文一致。"
                     "本机这三组每对平均保留 5.86–8.25 个相同特征，论文六组均值为 10.04–10.97。"
                     "第 1 组仅一个候选对。不同负载、Gear/FastCDC 和 Raw 基块资格都可能改变命中，"
                     "本次对照无法量化这些因素各自的影响。")
    report.links(("本机局部性表", "clean/table1-locality.csv"), ("论文表 1", "paper/table1.png"))

    report.heading(2, "新版计时下的阶段差异")
    report.paragraph("下表是每组 Delta Benchmark 的单次实跑，单位为 ms。CDC 为共享阶段。"
                     "构建内已包含 SF、索引、编码及即时回放，因此同一列中的子阶段不能与构建相加。"
                     "逐块恢复和文件摘要在构建结束后执行，不计入五轮单算法吞吐。")
    report.table(["数据集", "CDC", "两算法构建", "两算法 SF", "两算法 Delta 编码", "两算法即时校验",
                  "两算法逐块恢复", "两算法文件摘要累积"], [
        [r["dataset"], decimal(r["cdc_ms"]), pair(r, "encoded_pool_build_ms"),
         pair(r, "feature_extraction_ms"), pair(r, "delta_encode_ms"), pair(r, "roundtrip_verify_ms"),
         pair(r, "chunk_recovery_ms"), pair(r, "file_reassembly_ms")] for r in rows
    ])
    for i in (1, 2):
        r = rows[i]
        build_gap = float(r["ntransform_encoded_pool_build_ms"]) - float(r["finesse_encoded_pool_build_ms"])
        sf_gap = float(r["ntransform_feature_extraction_ms"]) - float(r["finesse_feature_extraction_ms"])
        encode_gap = float(r["ntransform_delta_encode_ms"]) - float(r["finesse_delta_encode_ms"])
        report.paragraph(f"第 {i + 1} 组 NTransform-Gear 的构建比 Finesse-Gear 多用 {build_gap:.1f} ms，"
                         f"其中 SF 多用 {sf_gap:.1f} ms，Delta 编码多用 {encode_gap:.1f} ms。"
                         "编码耗时差还包含匹配次数和参与编码的字节量差异。")
    report.links(("新版阶段表", "clean/stage-timing.csv"), ("编码延迟分布", "clean/delta-encoding.csv"),
                 ("资源记录", "clean/resources.csv"), ("整文件恢复核验", "clean/recovery.csv"))
    report.write()


if __name__ == "__main__":
    main()
