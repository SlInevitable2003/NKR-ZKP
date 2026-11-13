"""Concurrency qualification for the 90% utilization gate."""

from __future__ import annotations

from typing import Any

from .cases import Case
from .runner import CaseRunner


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
