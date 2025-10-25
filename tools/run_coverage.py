#!/usr/bin/env python3
"""Generate gcov-based coverage reports for ESP-IDF unit tests.

The script aggregates multiple ESP-IDF build directories instrumented for
coverage (``CONFIG_SENSOR_ENABLE_GCOV=y``, ``CONFIG_HMI_ENABLE_GCOV=y``,
``CONFIG_COMMON_ENABLE_GCOV=y``) after their Unity suites have executed and
emitted ``.gcda`` artefacts. It wraps ``gcovr`` to emit consolidated textual,
HTML, and Cobertura XML reports suitable for CI pipelines and also produces an
SVG badge reflecting the global line coverage.
"""

from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
from typing import List, Tuple


def _default_filters(repo_root: str) -> List[str]:
    return [
        os.path.join(repo_root, "sensor_node", "main"),
        os.path.join(repo_root, "hmi_node", "main"),
        os.path.join(repo_root, "common"),
    ]


def _build_gcovr_base(args: argparse.Namespace, repo_root: str) -> List[str]:
    filters = args.filter or _default_filters(repo_root)
    excludes = args.exclude or [r".*/tests/.*", r".*/build/.*"]

    cmd = [
        "gcovr",
        "--gcov-executable",
        args.gcov_tool,
        "--root",
        repo_root,
        "--branches",
    ]

    for build_dir in args.build_dirs:
        cmd.extend(["--object-directory", build_dir])

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


def _extract_line_metrics(report: dict) -> Tuple[int, int, float]:
    totals = report.get("totals", {})
    line_section = totals.get("lines") or totals.get("line") or {}

    covered = (
        line_section.get("covered")
        or line_section.get("covered_lines")
        or totals.get("lines_covered")
        or totals.get("covered_lines")
        or totals.get("line_covered")
        or 0
    )
    total = (
        line_section.get("count")
        or line_section.get("total")
        or totals.get("lines")
        or totals.get("lines_total")
        or totals.get("total_lines")
        or 0
    )
    percent = (
        line_section.get("percent")
        or line_section.get("percentage")
        or totals.get("line_percent")
        or totals.get("line_percentage")
        or totals.get("line_rate")
    )

    covered = int(covered or 0)
    total = int(total or 0)

    if percent is None and total:
        percent = covered / total
    if percent is None:
        percent = 0.0

    percent = float(percent)
    if percent <= 1.0:
        percent *= 100.0

    return covered, total, percent


def _coverage_color(percentage: float) -> str:
    if percentage >= 90.0:
        return "#4c1"
    if percentage >= 75.0:
        return "#dfb317"
    if percentage >= 60.0:
        return "#fe7d37"
    return "#e05d44"


def _coverage_badge_svg(label: str, percentage: float) -> str:
    value_text = f"{percentage:.1f}%"
    label_width = max(40, 6 * len(label) + 20)
    value_width = max(40, 6 * len(value_text) + 20)
    total_width = label_width + value_width
    color = _coverage_color(percentage)

    return (
        f"<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"{total_width}\" height=\"20\" role=\"img\" aria-label=\"{label}: {value_text}\">\n"
        "  <linearGradient id=\"s\" x2=\"0\" y2=\"100%\">\n"
        "    <stop offset=\"0\" stop-color=\"#bbb\" stop-opacity=\".1\"/>\n"
        "    <stop offset=\"1\" stop-opacity=\".1\"/>\n"
        "  </linearGradient>\n"
        f"  <mask id=\"m\">\n"
        f"    <rect width=\"{total_width}\" height=\"20\" rx=\"3\" fill=\"#fff\"/>\n"
        "  </mask>\n"
        "  <g mask=\"url(#m)\">\n"
        f"    <rect width=\"{label_width}\" height=\"20\" fill=\"#555\"/>\n"
        f"    <rect x=\"{label_width}\" width=\"{value_width}\" height=\"20\" fill=\"{color}\"/>\n"
        f"    <rect width=\"{total_width}\" height=\"20\" fill=\"url(#s)\"/>\n"
        "  </g>\n"
        "  <g fill=\"#fff\" text-anchor=\"middle\" font-family=\"DejaVu Sans,Verdana,Geneva,sans-serif\" font-size=\"11\">\n"
        f"    <text x=\"{label_width / 2:.1f}\" y=\"15\" fill=\"#010101\" fill-opacity=\".3\">{label}</text>\n"
        f"    <text x=\"{label_width / 2:.1f}\" y=\"14\">{label}</text>\n"
        f"    <text x=\"{label_width + value_width / 2:.1f}\" y=\"15\" fill=\"#010101\" fill-opacity=\".3\">{value_text}</text>\n"
        f"    <text x=\"{label_width + value_width / 2:.1f}\" y=\"14\">{value_text}</text>\n"
        "  </g>\n"
        "</svg>\n"
    )


def _write_badge(path: str, label: str, percentage: float) -> None:
    directory = os.path.dirname(path)
    if directory:
        os.makedirs(directory, exist_ok=True)
    with open(path, "w", encoding="utf-8") as handle:
        handle.write(_coverage_badge_svg(label, percentage))


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate gcovr coverage reports")
    parser.add_argument(
        "--build-dir",
        dest="build_dirs",
        action="append",
        default=None,
        help="ESP-IDF build directory containing gcda/gcno files. Repeat to aggregate multiple projects.",
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
    parser.add_argument(
        "--badge",
        default="coverage_badge.svg",
        help="Write an SVG badge with total line coverage to the given path (empty string to disable).",
    )
    parser.add_argument(
        "--badge-label",
        default="coverage",
        help="Label displayed on the generated coverage badge",
    )

    args = parser.parse_args()
    repo_root = os.path.abspath(os.path.join(os.path.dirname(__file__), os.pardir))

    if args.build_dirs:
        build_dirs = [os.path.abspath(path) for path in args.build_dirs]
    else:
        build_dirs = [
            os.path.join(repo_root, "sensor_node", "build"),
            os.path.join(repo_root, "hmi_node", "build"),
        ]

    existing_dirs: List[str] = []
    missing_dirs: List[str] = []
    for path in build_dirs:
        if os.path.isdir(path):
            if path not in existing_dirs:
                existing_dirs.append(path)
        else:
            missing_dirs.append(path)

    if not existing_dirs:
        searched = ", ".join(build_dirs)
        parser.error(
            "No valid build directories found. Instrument the targets with coverage and rerun. "
            f"Checked: {searched}"
        )

    if missing_dirs:
        sys.stderr.write(
            "warning: skipping missing build directories: " + ", ".join(missing_dirs) + "\n"
        )

    args.build_dirs = existing_dirs

    badge_path = args.badge.strip() if args.badge is not None else ""
    if badge_path:
        if not os.path.isabs(badge_path):
            badge_path = os.path.join(repo_root, badge_path)
        args.badge = badge_path
    else:
        args.badge = None

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

    json_cmd = base_cmd + ["--json", "-"]
    json_output = _run(json_cmd, capture=True)
    try:
        coverage_report = json.loads(json_output)
    except json.JSONDecodeError as exc:  # pragma: no cover - defensive
        raise RuntimeError("Failed to parse gcovr JSON output") from exc

    _, _, line_percent = _extract_line_metrics(coverage_report)
    line_percent = max(0.0, min(line_percent, 100.0))

    if args.badge:
        _write_badge(args.badge, args.badge_label, line_percent)

    return 0


if __name__ == "__main__":
    sys.exit(main())

