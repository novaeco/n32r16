#!/usr/bin/env python3
"""Generate gcov-based coverage reports for ESP-IDF unit tests.

The script expects a build directory produced with coverage instrumentation
enabled (``CONFIG_SENSOR_ENABLE_GCOV=y``) and executed unit tests so that the
``.gcda`` artefacts are present. It wraps ``gcovr`` to emit a textual summary
and optional HTML or XML reports suitable for CI pipelines.
"""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
from typing import List


def _default_filters(repo_root: str) -> List[str]:
    return [os.path.join(repo_root, "sensor_node", "main"), os.path.join(repo_root, "common")]


def _build_gcovr_base(args: argparse.Namespace, repo_root: str) -> List[str]:
    filters = args.filter or _default_filters(repo_root)
    excludes = args.exclude or [r".*/tests/.*", r".*/build/.*"]

    cmd = [
        "gcovr",
        "--gcov-executable",
        args.gcov_tool,
        "--root",
        repo_root,
        "--object-directory",
        args.build_dir,
        "--branches",
    ]

    for pattern in filters:
        cmd.extend(["--filter", pattern])
    for pattern in excludes:
        cmd.extend(["--exclude", pattern])

    return cmd


def _run(cmd: List[str], *, capture: bool = False) -> str:
    result = subprocess.run(cmd, check=True, capture_output=capture, text=True)
    if capture:
        return result.stdout
    return ""


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate gcovr coverage reports")
    parser.add_argument(
        "--build-dir",
        default=os.path.join("sensor_node", "build"),
        help="ESP-IDF build directory containing gcda/gcno files",
    )
    parser.add_argument(
        "--summary",
        default=None,
        help="Write the textual summary to the specified path instead of stdout",
    )
    parser.add_argument(
        "--html",
        default=None,
        help="Emit an HTML report (with details) to the provided path",
    )
    parser.add_argument(
        "--xml",
        default=None,
        help="Emit a Cobertura XML report to the provided path for CI systems",
    )
    parser.add_argument(
        "--filter",
        action="append",
        help="Additional gcovr filter regex applied to source paths",
    )
    parser.add_argument(
        "--exclude",
        action="append",
        help="Additional gcovr exclude regex applied to source paths",
    )
    parser.add_argument(
        "--gcov-tool",
        default=os.environ.get("GCOV", "xtensa-esp32s3-elf-gcov"),
        help="Path to the gcov binary compatible with the ESP32-S3 toolchain",
    )

    args = parser.parse_args()
    repo_root = os.path.abspath(os.path.join(os.path.dirname(__file__), os.pardir))
    args.build_dir = os.path.abspath(args.build_dir)

    if not os.path.isdir(args.build_dir):
        parser.error(f"Build directory '{args.build_dir}' does not exist")

    if shutil.which("gcovr") is None:
        parser.error("gcovr executable not found in PATH. Install it with 'pip install gcovr'.")

    if shutil.which(args.gcov_tool) is None:
        parser.error(f"gcov tool '{args.gcov_tool}' not found in PATH")

    base_cmd = _build_gcovr_base(args, repo_root)

    summary_target = args.summary or "-"
    summary_cmd = base_cmd + ["--txt", summary_target]

    if args.summary is None:
        output = _run(summary_cmd, capture=True)
        sys.stdout.write(output)
    else:
        _run(summary_cmd)

    if args.html:
        html_cmd = base_cmd + ["--html", "--html-details", args.html]
        _run(html_cmd)

    if args.xml:
        xml_cmd = base_cmd + ["--xml", args.xml]
        _run(xml_cmd)

    return 0


if __name__ == "__main__":
    sys.exit(main())

