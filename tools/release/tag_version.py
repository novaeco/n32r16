#!/usr/bin/env python3
"""Create a signed, annotated Git tag that matches the repository VERSION file."""
from __future__ import annotations

import argparse
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
VERSION_FILE = ROOT / "VERSION"


def run(cmd: list[str], check: bool = True) -> subprocess.CompletedProcess[str]:
    return subprocess.run(cmd, cwd=ROOT, check=check, text=True, capture_output=True)


def ensure_clean_tree() -> None:
    status = run(["git", "status", "--porcelain"]).stdout.strip()
    if status:
        print("Refusing to tag: working tree is dirty", file=sys.stderr)
        print(status, file=sys.stderr)
        raise SystemExit(1)


def get_version() -> str:
    if not VERSION_FILE.exists():
        raise SystemExit("VERSION file not found")
    version = VERSION_FILE.read_text(encoding="utf-8").strip()
    if not version:
        raise SystemExit("VERSION file is empty")
    if not version[0].isdigit():
        raise SystemExit(f"VERSION must start with a digit, got {version!r}")
    return version


def tag_exists(tag: str) -> bool:
    result = run(["git", "tag", "-l", tag], check=False)
    return tag in result.stdout.split()


def create_tag(tag: str, message: str, sign: bool) -> None:
    cmd = ["git", "tag", tag, "-a", "-m", message]
    if sign:
        cmd.insert(2, "-s")
    run(cmd)


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--no-sign", action="store_true", help="Do not sign the tag")
    parser.add_argument("--dry-run", action="store_true", help="Validate inputs without creating a tag")
    args = parser.parse_args(argv)

    version = get_version()
    tag = f"v{version}"
    ensure_clean_tree()

    if tag_exists(tag):
        print(f"Tag {tag} already exists", file=sys.stderr)
        return 1

    message_lines = [
        f"novaeco/n32r16 {tag}",
        "",
        "Reproducible release tag generated from VERSION file.",
    ]
    message = "\n".join(message_lines)

    if args.dry_run:
        print(f"Would create {'signed ' if not args.no_sign else ''}tag {tag}\n\n{message}")
        return 0

    create_tag(tag, message, sign=not args.no_sign)
    print(f"Created tag {tag}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
