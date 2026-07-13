#!/usr/bin/env python3
"""Create an interview report from a JSON session.

Input example:
{
  "target": "嵌入式 Linux 工程师",
  "questions": [
    {"question": "...", "answer": "...", "score": 3, "feedback": "..."}
  ],
  "dimension_scores": {
    "技术准确性": 3,
    "项目所有权": 4,
    "调试能力": 3,
    "设计推理": 2,
    "表达沟通": 3,
    "追问深度": 2
  }
}
"""

from __future__ import annotations

import argparse
from datetime import date
import json
from pathlib import Path
import sys


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("session_json")
    parser.add_argument("--output", default="docs/interview-report.md")
    args = parser.parse_args()

    source = Path(args.session_json)
    try:
        data = json.loads(source.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        print(f"Cannot read session: {exc}", file=sys.stderr)
        return 2

    scores = data.get("dimension_scores", {})
    total = sum(float(v) for v in scores.values())

    lines = [
        "# 模拟面试报告",
        "",
        f"- 日期：{date.today().isoformat()}",
        f"- 目标岗位：{data.get('target', '未填写')}",
        f"- 总分：{total:g} / 30",
        "",
        "## 分项评分",
        "",
        "| 维度 | 分数 |",
        "|---|---:|",
    ]
    lines += [f"| {k} | {v} |" for k, v in scores.items()]

    lines += ["", "## 问答记录", ""]
    for index, item in enumerate(data.get("questions", []), 1):
        lines += [
            f"### Q{index}. {item.get('question', '')}",
            "",
            f"**回答：** {item.get('answer', '')}",
            "",
            f"**评分：** {item.get('score', '')}",
            "",
            f"**反馈：** {item.get('feedback', '')}",
            "",
        ]

    lines += [
        "## 三个强项",
        "",
        "- ",
        "- ",
        "- ",
        "",
        "## 三个风险",
        "",
        "- ",
        "- ",
        "- ",
        "",
        "## 需要补做的实验",
        "",
        "- ",
        "",
        "## 下次模拟范围",
        "",
        "- ",
    ]

    output = Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
