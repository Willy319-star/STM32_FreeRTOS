#!/usr/bin/env python3
"""Validate the local skill package structure and basic metadata."""

from __future__ import annotations

from pathlib import Path
import re
import sys


def main() -> int:
    skill_dir = Path(__file__).resolve().parents[1]
    skill_file = skill_dir / "SKILL.md"

    errors = []
    if not skill_file.exists():
        errors.append("Missing SKILL.md")
    else:
        text = skill_file.read_text(encoding="utf-8")
        if not text.startswith("---"):
            errors.append("SKILL.md must start with YAML frontmatter")
        if not re.search(r"(?m)^name:\s*embedded-learning-coach\s*$", text):
            errors.append("Missing or incorrect name")
        if not re.search(r"(?m)^description:\s*>?\s*$", text):
            errors.append("Missing description")
        if len(text) < 1000:
            errors.append("SKILL.md is unexpectedly short")

    required_dirs = ["references", "scripts", "assets"]
    for name in required_dirs:
        if not (skill_dir / name).is_dir():
            errors.append(f"Missing directory: {name}")

    if errors:
        for error in errors:
            print(f"ERROR: {error}")
        return 1

    print(f"Skill looks valid: {skill_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
