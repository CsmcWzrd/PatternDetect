#!/usr/bin/env python3
"""Run the deterministic generated PatternDetect large suite."""
from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
import time
import re
import tempfile
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path
from typing import Dict, List


def load_manifest(suite: Path) -> Dict:
    mf = suite / "manifest.json"
    if not mf.exists():
        raise FileNotFoundError(f"manifest not found: {mf}; run tests/generate_large_suite.py first")
    return json.loads(mf.read_text(encoding="utf-8"))


def verify_operation_logs(prefix: Path, expected_mode: str) -> List[str]:
    errors: List[str] = []
    human = Path(str(prefix) + ".human.log")
    script = Path(str(prefix) + ".script.log")
    want_human = expected_mode != "script"
    want_script = expected_mode != "human"
    if human.exists() != want_human or script.exists() != want_script:
        return [f"unexpected operation log files: human={human.exists()}, script={script.exists()}, mode={expected_mode}"]
    if want_human:
        data = human.read_text(encoding="utf-8")
        if not data.endswith("\n\n\n"):
            errors.append("human log has no double-empty-line terminator")
        for physical in data.splitlines():
            if physical and (not physical.startswith("    ") or len(physical) > 128):
                errors.append("human log indentation or 128-column width invalid")
                break
    if want_script:
        entries = script.read_text(encoding="utf-8").splitlines()
        if not entries:
            errors.append("script log empty")
        previous = 0
        for entry in entries:
            fields = entry.split("  -----  ")
            if len(fields) != 5:
                errors.append("script log does not contain five fields")
                break
            header = fields[0].split("  )))))  ")
            if (len(header) != 5 or not re.fullmatch(r"[0-9]{16}", header[0])
                    or not re.fullmatch(r"\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}\.\d{6}Z", header[2])):
                errors.append("script header invalid")
                break
            m = re.search(r"event=(\d+);parent=(\d+)", fields[2])
            if not m or int(m[1]) <= previous or int(m[2]) >= int(m[1]):
                errors.append("script event hierarchy invalid")
                break
            previous = int(m[1])
        if want_human and len(entries) != human.read_text(encoding="utf-8").count("\n\n\n"):
            errors.append("human and script record counts differ")
    return errors


def run_one(binary: Path, suite: Path, case: Dict, timeout: int, output_dir: Path) -> List[str]:
    config = suite / case["config"]
    log = suite / case["log"]
    prefix = output_dir / f"test_{case['case_id']:04d}"
    mode = ("both", "human", "script")[case["case_id"] % 3]
    cmd = [str(binary), "--config", str(config), "--log", str(log),
           "--operation-log-prefix", str(prefix)]
    if mode == "human": cmd.append("--human-readable-only")
    if mode == "script": cmd.append("--script-readable-only")
    proc = subprocess.run(cmd, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=timeout)
    errors: List[str] = []
    if proc.returncode != 0:
        errors.append(f"exit={proc.returncode}; stderr={proc.stderr.strip()[:500]}")
    combined = proc.stdout + "\n" + proc.stderr
    for expected in case["expected_reports"]:
        needle = "REPORT: " + expected
        if needle not in combined:
            errors.append(f"missing report: {expected}")
            break
    errors.extend(verify_operation_logs(prefix, mode))
    return errors


def run_expression_tests(binary: Path) -> List[str]:
    tests = {
        "add(multiply(2,3),4)": "10",
        "round(rad2deg(atan2(1,1)))": "45",
        "upper(concat(\"pat\",\"tern\"))": "PATTERN",
        "if(and(gt(10,4),contains(\"abcdef\",\"bcd\")),\"ok\",\"bad\")": "ok",
        "join(split(\"a:b:c\",\":\"),\"-\")": "a-b-c",
    }
    errors: List[str] = []
    for expr, expected in tests.items():
        with tempfile.TemporaryDirectory(prefix="patterndetect_expr_") as folder:
            proc = subprocess.run([str(binary), "--operation-log-prefix", str(Path(folder) / "expr"), "--expr", expr], text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=10)
        got = proc.stdout.strip()
        if proc.returncode != 0 or got != expected:
            errors.append(f"expr {expr!r}: expected {expected!r}, got {got!r}, rc={proc.returncode}, stderr={proc.stderr.strip()}")
    return errors


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--binary", required=True, help="path to patterndetect executable")
    ap.add_argument("--suite", default="tests/large_suite")
    ap.add_argument("--limit", type=int, default=0, help="run only the first N cases; 0 means all")
    ap.add_argument("--timeout", type=int, default=20)
    ap.add_argument("--jobs", type=int, default=max(1, min(os.cpu_count() or 1, 8)), help="parallel subprocess jobs")
    ap.add_argument("--report", default="tests/LARGE_SUITE_VALIDATION.md")
    ap.add_argument("--keep-operation-logs", action="store_true", help="keep generated per-case operation logs")
    args = ap.parse_args()

    binary = Path(args.binary).resolve()
    suite = Path(args.suite).resolve()
    report = Path(args.report).resolve()
    manifest = load_manifest(suite)
    cases = manifest["cases"][: args.limit or None]

    start = time.time()
    temp_context = tempfile.TemporaryDirectory(prefix="patterndetect_suite_")
    output_dir = Path(temp_context.name)
    if args.keep_operation_logs:
        output_dir = Path(args.report).resolve().parent / "large_suite_operation_logs"
        output_dir.mkdir(parents=True, exist_ok=True)
    failures: List[str] = []
    expr_errors = run_expression_tests(binary)
    for e in expr_errors:
        failures.append("expression: " + e)

    def case_task(case: Dict) -> str:
        try:
            errors = run_one(binary, suite, case, args.timeout, output_dir)
        except subprocess.TimeoutExpired:
            errors = [f"timeout after {args.timeout}s"]
        except Exception as exc:  # pragma: no cover - defensive runner reporting
            errors = [f"runner exception: {exc}"]
        if errors:
            return f"case {case['case_id']:04d}: " + "; ".join(errors)
        return ""

    completed = 0
    with ThreadPoolExecutor(max_workers=max(1, args.jobs)) as pool:
        futures = [pool.submit(case_task, case) for case in cases]
        for fut in as_completed(futures):
            completed += 1
            failure = fut.result()
            if failure:
                failures.append(failure)
            if completed % 100 == 0 or completed == len(cases):
                print(f"validated {completed}/{len(cases)} cases")

    elapsed = time.time() - start
    coverage = manifest["coverage"]
    lines = [
        "# PatternDetect large suite validation",
        "",
        f"- Binary: `{binary}`",
        f"- Suite: `{suite}`",
        f"- Cases executed: {len(cases)} of {manifest['case_count']}",
        f"- Parallel jobs: {max(1, args.jobs)}",
        f"- Expression tests executed: 5",
        f"- Failures: {len(failures)}",
        f"- Elapsed seconds: {elapsed:.3f}",
        "",
        "## Coverage from manifest",
        "",
    ]
    for key, value in coverage.items():
        lines.append(f"- {key}: {value}")
    lines.extend(["", "## Result", ""])
    if failures:
        lines.append("FAILED")
        lines.append("")
        for failure in failures[:100]:
            lines.append(f"- {failure}")
        if len(failures) > 100:
            lines.append(f"- ... {len(failures) - 100} more failures")
    else:
        lines.append("PASSED")
    report.parent.mkdir(parents=True, exist_ok=True)
    report.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"PatternDetect large suite: {len(cases)} cases, failures={len(failures)}, elapsed={elapsed:.3f}s")
    print(f"Validation report: {report}")
    if not args.keep_operation_logs: temp_context.cleanup()
    return 0 if not failures else 1

if __name__ == "__main__":
    raise SystemExit(main())
