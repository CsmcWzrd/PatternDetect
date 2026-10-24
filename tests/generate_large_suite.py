#!/usr/bin/env python3
"""Generate a deterministic 1000-case PatternDetect validation suite.

The suite intentionally reuses fake log families across many configs.  Each
family uses one stable filename prefix (`app.log`) and, when rotation is
requested, rotated siblings (`app.log.1` ... `app.log.10`).
"""
from __future__ import annotations

import argparse
import json
import random
import shutil
from datetime import datetime, timedelta
from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict, List, Tuple

SEED = 20260705
DEFAULT_CASES = 1000
DEFAULT_LOGSETS = 100
COMPONENTS = [f"Component{i:03d}" for i in range(100)]
SIZE_BUCKETS = [
    300, 350, 400, 500, 650, 800, 1000, 1200, 1500, 1800,
    2200, 2600, 3000, 3500, 4000, 5000, 6000, 7000, 8000, 9000,
    10000,
]

@dataclass
class PatternPlan:
    pattern_index: int
    pattern_id: str
    steps: int
    component: str
    children: List[str] = field(default_factory=list)

@dataclass
class CasePlan:
    case_id: int
    logset_id: int
    rotation_count: int
    target_lines: int
    pattern_count: int
    chain_depth: int
    fanout: int
    plans: List[PatternPlan]
    expected_reports: List[str]
    config_relpath: str
    expected_relpath: str
    log_relpath: str


def timestamp_for(case_id: int, pattern_index: int, step: int) -> str:
    # Monotonic timestamps keep `within timestamp:1a + delta` checks stable
    # for every step and child pattern in a generated chain.
    dt = datetime(2026, 1, 1, 0, 0, 0) + timedelta(days=case_id, seconds=pattern_index * 1000 + step * 10)
    return dt.strftime("%Y-%m-%d %H:%M:%S UTC")


def step_count(case_id: int, pattern_index: int) -> int:
    # Most sections are small for quick execution, but the full suite includes
    # the requested maximum of 10 log-message statements in many places.
    if (case_id + pattern_index) % 97 == 0:
        return 10
    if (case_id * 5 + pattern_index) % 53 == 0:
        return 7
    return 1 + ((case_id + pattern_index * 2) % 3)


def build_case(case_id: int, logset_count: int) -> CasePlan:
    logset_id = case_id % logset_count
    rotation_count = logset_id % 11
    target_lines = SIZE_BUCKETS[logset_id % len(SIZE_BUCKETS)]
    pattern_count = 5 + (case_id % 6)  # 5..10
    chain_depth = 1 + ((case_id * 7) % pattern_count)  # observed depth range includes 1..10
    plans: List[PatternPlan] = []
    for pidx in range(pattern_count):
        plans.append(PatternPlan(
            pattern_index=pidx,
            pattern_id=f"P{pidx:02d}",
            steps=step_count(case_id, pidx),
            component=COMPONENTS[(case_id * 13 + pidx * 17) % len(COMPONENTS)],
        ))

    # Main linear chain: P00 -> P01 -> ... up to requested chain depth.
    for pidx in range(max(0, chain_depth - 1)):
        plans[pidx].children.append(plans[pidx + 1].pattern_id)

    # Additional branch/fanout coverage without creating cycles.  Fanout can be
    # 0 for depth-1 independent-root cases, and reaches up to 9 when possible.
    if chain_depth > 1:
        for pidx in range(chain_depth, pattern_count):
            plans[0].children.append(plans[pidx].pattern_id)
    fanout = len(plans[0].children)

    expected_reports = [f"CASE {case_id:04d} PATTERN {p.pattern_id} HIT" for p in plans]
    return CasePlan(
        case_id=case_id,
        logset_id=logset_id,
        rotation_count=rotation_count,
        target_lines=target_lines,
        pattern_count=pattern_count,
        chain_depth=chain_depth,
        fanout=fanout,
        plans=plans,
        expected_reports=expected_reports,
        config_relpath=f"configs/case_{case_id:04d}.pdconf",
        expected_relpath=f"expected/case_{case_id:04d}.expected",
        log_relpath=f"logs/logset_{logset_id:03d}/app.log",
    )


def event_lines_for_case(case: CasePlan) -> List[str]:
    lines: List[str] = []
    # Emit in pattern-index order. Children search forward from the parent
    # cursor, so this ordering validates multi-level chaining and fanout.
    for plan in case.plans:
        for step in range(plan.steps):
            marker = f"PDET_C{case.case_id:04d}_{plan.pattern_id}_S{step:02d}"
            status = f"OK_C{case.case_id:04d}_{plan.pattern_id}_S{step:02d}"
            ts = timestamp_for(case.case_id, plan.pattern_index, step)
            lines.append(
                f"{ts} {plan.component} {marker} {ts} {status} payload_{step} chainDepth_{case.chain_depth} fanout_{case.fanout}"
            )
    return lines


def write_config(output: Path, case: CasePlan) -> None:
    path = output / case.config_relpath
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8") as f:
        f.write("{\n")
        f.write("    use chronological-order\n")
        if case.rotation_count > 0:
            f.write("    use log-rotated\n")
        f.write("    use report-destination terminal\n\n")
        for plan in case.plans:
            f.write(f"    {plan.pattern_id}: [\n")
            f.write(f"        title \"Case {case.case_id:04d} pattern {plan.pattern_id}\"\n")
            if plan.pattern_index > 0:
                f.write("        within timestamp:1a + 2678400\n")
            for step in range(plan.steps):
                marker = f"PDET_C{case.case_id:04d}_{plan.pattern_id}_S{step:02d}"
                f.write(f"        log message {plan.component} {marker} separator \" \"\n")
                f.write("        grab timestamp:1a, value:1status, value:1tail@\n")
            last_status = f"OK_C{case.case_id:04d}_{plan.pattern_id}_S{plan.steps - 1:02d}"
            f.write(f"        check value:1status {last_status} equals\n")
            f.write(f"        report \"CASE {case.case_id:04d} PATTERN {plan.pattern_id} HIT\" match:file match:line value:1status\n")
            for child in plan.children:
                f.write(f"        on-hit: {child}\n")
            f.write("    ]\n\n")
        f.write("}\n")


def write_expected(output: Path, case: CasePlan) -> None:
    path = output / case.expected_relpath
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(case.expected_reports) + "\n", encoding="utf-8")


def filler_line(logset_id: int, line_no: int) -> str:
    comp = COMPONENTS[(logset_id * 19 + line_no * 7) % len(COMPONENTS)]
    day = 1 + ((logset_id + line_no) % 24)
    hour = line_no % 24
    minute = (line_no * 3) % 60
    second = (line_no * 7) % 60
    return (
        f"2026-06-{day:02d} {hour:02d}:{minute:02d}:{second:02d} UTC "
        f"{comp} background noise metric={line_no % 997} state=steady"
    )


def write_logsets(output: Path, cases: List[CasePlan], logset_count: int) -> Dict[int, Dict[str, int]]:
    grouped: Dict[int, List[str]] = {i: [] for i in range(logset_count)}
    metadata: Dict[int, Dict[str, int]] = {}
    for case in cases:
        grouped[case.logset_id].extend(event_lines_for_case(case))

    rng = random.Random(SEED)
    logs_root = output / "logs"
    logs_root.mkdir(parents=True, exist_ok=True)
    for logset_id in range(logset_count):
        rotation_count = logset_id % 11
        target_lines = SIZE_BUCKETS[logset_id % len(SIZE_BUCKETS)]
        lines = list(grouped[logset_id])
        minimum_filler = 25
        while len(lines) < max(target_lines, len(grouped[logset_id]) + minimum_filler):
            lines.append(filler_line(logset_id, len(lines) + 1))
        # Keep event order intact by only shuffling filler after injected events.
        event_count = len(grouped[logset_id])
        filler = lines[event_count:]
        rng.shuffle(filler)
        lines = lines[:event_count] + filler

        root = logs_root / f"logset_{logset_id:03d}"
        root.mkdir(parents=True, exist_ok=True)
        for old in root.glob("app.log*"):
            old.unlink()
        file_count = rotation_count + 1
        chunk = (len(lines) + file_count - 1) // file_count
        for idx in range(file_count):
            name = "app.log" if idx == 0 else f"app.log.{idx}"
            start = idx * chunk
            end = min(len(lines), start + chunk)
            (root / name).write_text("\n".join(lines[start:end]) + "\n", encoding="utf-8")
        metadata[logset_id] = {
            "rotation_count": rotation_count,
            "target_total_lines": target_lines,
            "actual_total_lines": len(lines),
            "injected_event_lines": event_count,
            "file_count": file_count,
        }
    return metadata


def write_manifest(output: Path, cases: List[CasePlan], logsets: Dict[int, Dict[str, int]]) -> None:
    manifest = {
        "seed": SEED,
        "case_count": len(cases),
        "logset_count": len(logsets),
        "component_count": len(COMPONENTS),
        "component_names": COMPONENTS,
        "coverage": {
            "pattern_count_range": [min(c.pattern_count for c in cases), max(c.pattern_count for c in cases)],
            "chain_depth_range": [min(c.chain_depth for c in cases), max(c.chain_depth for c in cases)],
            "fanout_range": [min(c.fanout for c in cases), max(c.fanout for c in cases)],
            "pattern_step_range": [min(p.steps for c in cases for p in c.plans), max(p.steps for c in cases for p in c.plans)],
            "rotation_count_range": [min(v["rotation_count"] for v in logsets.values()), max(v["rotation_count"] for v in logsets.values())],
            "log_line_count_range": [min(v["actual_total_lines"] for v in logsets.values()), max(v["actual_total_lines"] for v in logsets.values())],
        },
        "logsets": {f"logset_{k:03d}": v for k, v in logsets.items()},
        "cases": [
            {
                "case_id": c.case_id,
                "config": c.config_relpath,
                "expected": c.expected_relpath,
                "log": c.log_relpath,
                "logset_id": c.logset_id,
                "rotation_count": c.rotation_count,
                "pattern_count": c.pattern_count,
                "chain_depth": c.chain_depth,
                "fanout": c.fanout,
                "expected_reports": c.expected_reports,
            }
            for c in cases
        ],
    }
    (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")


def write_readme(output: Path, cases: List[CasePlan], logsets: Dict[int, Dict[str, int]]) -> None:
    coverage = {
        "cases": len(cases),
        "fake log sets": len(logsets),
        "components": len(COMPONENTS),
        "pattern count range": f"{min(c.pattern_count for c in cases)}..{max(c.pattern_count for c in cases)}",
        "chain depth range": f"{min(c.chain_depth for c in cases)}..{max(c.chain_depth for c in cases)}",
        "fanout range": f"{min(c.fanout for c in cases)}..{max(c.fanout for c in cases)}",
        "pattern step range": f"{min(p.steps for c in cases for p in c.plans)}..{max(p.steps for c in cases for p in c.plans)}",
        "rotation count range": f"{min(v['rotation_count'] for v in logsets.values())}..{max(v['rotation_count'] for v in logsets.values())}",
        "log line count range": f"{min(v['actual_total_lines'] for v in logsets.values())}..{max(v['actual_total_lines'] for v in logsets.values())}",
    }
    lines = ["# PatternDetect generated large suite", ""]
    lines.append("This directory is generated by `tests/generate_large_suite.py`.")
    lines.append("The cases intentionally reuse fake logs while varying rotation count, component name, pattern count, pattern section length, fanout, and chain depth.")
    lines.append("")
    lines.append("## Coverage")
    lines.append("")
    for k, v in coverage.items():
        lines.append(f"- {k}: {v}")
    lines.append("")
    lines.append("Run from the project root after building PatternDetect:")
    lines.append("")
    lines.append("```sh")
    lines.append("python3 tests/run_large_suite.py --binary ./build/patterndetect")
    lines.append("```")
    (output / "README.md").write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--output", default="tests/large_suite", help="suite output directory")
    ap.add_argument("--cases", type=int, default=DEFAULT_CASES)
    ap.add_argument("--logsets", type=int, default=DEFAULT_LOGSETS)
    ap.add_argument("--force", action="store_true")
    args = ap.parse_args()

    output = Path(args.output)
    if output.exists() and args.force:
        shutil.rmtree(output)
    output.mkdir(parents=True, exist_ok=True)

    cases = [build_case(i, args.logsets) for i in range(args.cases)]
    logsets = write_logsets(output, cases, args.logsets)
    for case in cases:
        write_config(output, case)
        write_expected(output, case)
    write_manifest(output, cases, logsets)
    write_readme(output, cases, logsets)
    print(f"Generated {len(cases)} cases, {len(logsets)} reusable log sets, {len(COMPONENTS)} components at {output}")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
