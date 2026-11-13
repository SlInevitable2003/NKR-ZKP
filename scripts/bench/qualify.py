"""Concurrency qualification for the 90% utilization gate."""

from __future__ import annotations

from pathlib import Path
from typing import Any

from .cases import Case
from .config import Config
from .runner import CaseRunner
from .util import ensure_dir, read_json, write_json


def candidate_concurrency(max_value: int) -> list[int]:
    values: list[int] = []
    value = 1
    while value <= max_value:
        values.append(value)
        value *= 2
    if values[-1] != max_value:
        values.append(max_value)
    return values


def qualify_case(
    runner: CaseRunner,
    case: Case,
    *,
    max_concurrency: int,
    target: float,
) -> dict[str, Any]:
    runner.prepare(case)
    candidates = candidate_concurrency(max(1, max_concurrency))
    attempts: list[dict[str, Any]] = []
    best: dict[str, Any] | None = None
    for p in candidates:
        result = runner._run_once(case, p, 0)  # noqa: SLF001 - intentional internal API
        util = result.util.get("util_mean")
        throughput = result.throughput_proofs_s
        record = {
            "concurrency": p,
            "util_mean": util,
            "throughput_proofs_s": throughput,
            "t_max_ms": result.t_max_ms,
            "status": result.status,
            "note": result.note,
        }
        attempts.append(record)
        if util is not None and util >= target * 100.0:
            if best is None or (throughput or 0.0) > (best.get("throughput_proofs_s") or 0.0):
                best = record
    if best is None:
        # No qualifying p.  Record the highest-utilization attempt for the audit trail.
        best = max(
            attempts,
            key=lambda item: (item.get("util_mean") or -1.0, item.get("concurrency") or 0),
        )
        best = dict(best)
        best["qualified"] = False
    else:
        best = dict(best)
        best["qualified"] = True
    return {
        "case_id": case.case_id,
        "target": target,
        "attempts": attempts,
        **best,
    }


def qualify_cases(
    config: Config,
    cases: list[Case],
    run_dir: Path,
    *,
    max_concurrency: int | None = None,
    target: float | None = None,
) -> dict[str, Any]:
    runner = CaseRunner(config, run_dir)
    target = target if target is not None else float(
        config.get("utilization", "target", default=0.90)
    )
    qualification_path = run_dir / "qualification.json"
    existing = read_json(qualification_path, default={})
    if not isinstance(existing, dict):
        existing = {}
    for case in cases:
        if case.platform == "cpu" or case.suite == "p3":
            continue
        limit = max_concurrency
        if limit is None:
            limit = int(
                config.get("performance", case.suite, "max_concurrency", default=8)
            )
        entry = qualify_case(
            runner, case, max_concurrency=limit, target=target
        )
        existing[case.case_id] = entry
        audit_dir = ensure_dir(run_dir / "audit")
        write_json(audit_dir / f"{case.case_id}.json", entry)
        write_json(qualification_path, existing)
    return existing
