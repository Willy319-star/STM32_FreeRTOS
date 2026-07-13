#!/usr/bin/env python3
"""Create a dated embedded-learning Markdown log from a template."""

from __future__ import annotations

import argparse
from datetime import date
from pathlib import Path
import re
import sys


def slugify(text: str) -> str:
    text = text.strip().lower()
    text = re.sub(r"[^\w\u4e00-\u9fff-]+", "-", text, flags=re.UNICODE)
    return text.strip("-") or "learning"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--topic", required=True, help="Learning topic")
    parser.add_argument(
        "--mode",
        choices=["LEARN", "BUILD", "DEBUG", "REVIEW", "INTERVIEW", "RESUME"],
        default="LEARN",
    )
    parser.add_argument("--output-dir", default="docs/learning")
    parser.add_argument("--force", action="store_true")
    args = parser.parse_args()

    out_dir = Path(args.output_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    out = out_dir / f"{date.today().isoformat()}-{slugify(args.topic)}.md"

    if out.exists() and not args.force:
        print(f"Refusing to overwrite: {out}", file=sys.stderr)
        return 2

    content = f"""# {args.topic}

- 日期：{date.today().isoformat()}
- 模式：{args.mode}
- 仓库提交：
- 硬件：
- 软件版本：

## 学习目标

## 前置知识

## 我的预测

## 数据路径 / 时序

## 实现或调试步骤

## 观测方法

## 原始证据

## 实际结果

## 与预测的差异

## 原因解释

## 故障注入与边界测试

## 结论

## 五道面试题

## 下一步
"""
    out.write_text(content, encoding="utf-8")
    print(out)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
