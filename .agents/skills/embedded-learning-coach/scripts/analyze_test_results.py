#!/usr/bin/env python3
"""Analyze CSV test results and write a Markdown summary.

Expected columns:
  timestamp_ms, latency_ms, success, error_type

`success` accepts: 1/0, true/false, yes/no.
"""

from __future__ import annotations

import argparse
import csv
from collections import Counter
from pathlib import Path
import statistics
import sys


TRUE_VALUES = {"1", "true", "yes", "y", "pass", "passed"}


def percentile(values: list[float], p: float) -> float:
    if not values:
        return float("nan")
    values = sorted(values)
    if len(values) == 1:
        return values[0]
    position = (len(values) - 1) * p
    lower = int(position)
    upper = min(lower + 1, len(values) - 1)
    weight = position - lower
    return values[lower] * (1 - weight) + values[upper] * weight


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("csv_file")
    parser.add_argument("--output", default="docs/test-summary.md")
    args = parser.parse_args()

    source = Path(args.csv_file)
    if not source.exists():
        print(f"Missing input: {source}", file=sys.stderr)
        return 2

    rows = []
    with source.open(newline="", encoding="utf-8-sig") as f:
        reader = csv.DictReader(f)
        required = {"timestamp_ms", "latency_ms", "success", "error_type"}
        missing = required - set(reader.fieldnames or [])
        if missing:
            print(f"Missing columns: {sorted(missing)}", file=sys.stderr)
            return 2
        rows = list(reader)

    latencies = []
    errors = Counter()
    success_count = 0
    invalid_rows = 0

    for row in rows:
        try:
            latency = float(row["latency_ms"])
        except (TypeError, ValueError):
            invalid_rows += 1
            continue
        latencies.append(latency)
        success = str(row["success"]).strip().lower() in TRUE_VALUES
        if success:
            success_count += 1
        else:
            errors[(row["error_type"] or "unspecified").strip()] += 1

    valid = len(latencies)
    success_rate = (success_count / valid * 100) if valid else 0.0

    lines = [
        "# 测试结果摘要",
        "",
        f"- 输入文件：`{source}`",
        f"- 总行数：{len(rows)}",
        f"- 有效行数：{valid}",
        f"- 无效行数：{invalid_rows}",
        f"- 成功率：{success_rate:.2f}%",
        "",
        "## 时延",
        "",
    ]

    if latencies:
        lines += [
            f"- 平均值：{statistics.fmean(latencies):.3f} ms",
            f"- 中位数：{statistics.median(latencies):.3f} ms",
            f"- P95：{percentile(latencies, 0.95):.3f} ms",
            f"- P99：{percentile(latencies, 0.99):.3f} ms",
            f"- 最小值：{min(latencies):.3f} ms",
            f"- 最大值：{max(latencies):.3f} ms",
        ]
    else:
        lines.append("- 无有效时延数据")

    lines += ["", "## 错误分布", ""]
    if errors:
        lines += ["| 错误类型 | 次数 |", "|---|---:|"]
        lines += [f"| {name} | {count} |" for name, count in errors.most_common()]
    else:
        lines.append("- 无失败记录")

    lines += [
        "",
        "## 解释前必须补充",
        "",
        "- 硬件与软件版本",
        "- 数据产生速率",
        "- 负载大小",
        "- 测量起点和终点",
        "- warm-up 与稳定阶段",
        "- 是否包含队列、网络或后处理时间",
    ]

    output = Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
