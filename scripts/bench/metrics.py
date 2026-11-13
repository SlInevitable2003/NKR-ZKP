"""Timing, throughput and utilization aggregation."""

from __future__ import annotations

import csv
import statistics
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

from .cases import Case
from .executor import ProcessResult
from .monitor import Sample, summarize_devices
from .protocols import (
    backend_line,
    expected_backend,
    parse_profile,
    parse_stark,
    profile_label,
    program_total_label,
    verify_ok_text,
    write_proof_label,
)
from .util import ensure_dir


@dataclass
class ProofRun:
    process: ProcessResult
    proof_path: Path
    verify_process: ProcessResult | None
    profile: dict[str, float]
    stark: dict[str, Any]
    prove_ms: float | None
    program_ms: float | None
    write_ms: float | None
    online_start: float
    online_end: float
    backend_ok: bool
    proof_valid: bool
    note: str = ""


@dataclass
class RepetitionResult:
    case: Case
    repetition: int
    runs: list[ProofRun] = field(default_factory=list)
    samples: list[Sample] = field(default_factory=list)
    util: dict[str, Any] = field(default_factory=dict)
    status: str = "ok"
    note: str = ""
    wave: int | None = None

    @property
    def m(self) -> int:
        return len(self.runs)

    @property
    def prove_times_ms(self) -> list[float]:
        return [run.prove_ms for run in self.runs if run.prove_ms is not None]

    @property
    def t_max_ms(self) -> float | None:
        values = self.prove_times_ms
        return max(values) if values else None

    @property
    def t_median_ms(self) -> float | None:
        values = self.prove_times_ms
        return statistics.median(values) if values else None

    @property
    def throughput_proofs_s(self) -> float | None:
        t_max = self.t_max_ms
        return None if not t_max or t_max <= 0 else self.m / (t_max / 1000.0)

    @property
    def throughput_constraints_s(self) -> float | None:
        if self.case.k is None:
            return None
        value = self.throughput_proofs_s
        return None if value is None else value * (1 << self.case.k)

    @property
    def proof_valid(self) -> bool:
        return bool(self.runs) and all(run.proof_valid for run in self.runs)

    @property
    def backend_ok(self) -> bool:
        return all(run.backend_ok for run in self.runs)


def build_proof_run(
    case: Case,
    process: ProcessResult,
    proof_path: Path,
    verify_process: ProcessResult | None,
) -> ProofRun:
    profile = parse_profile(process.stdout)
    stark = parse_stark(process.stdout)
    prove_label = profile_label(case.protocol) if case.protocol != "stark" else "stark"
    if case.protocol == "stark":
        prove_ms = stark.get("prove_total_ms")
        program_ms = prove_ms
        write_ms = 0.0
    else:
        prove_ms = profile.get(prove_label)
        program_ms = profile.get(program_total_label(case.protocol))
        write_ms = profile.get(write_proof_label(case.protocol), 0.0)

    backend_ok = True
    if case.protocol != "stark":
        marker = f"{backend_line(case.protocol)} {expected_backend(case.platform)}"
        backend_ok = marker in process.stdout
    if case.protocol == "stark":
        proof_valid = (
            process.ok
            and "Proof verified in" in process.stdout
            and "Failed to verify proof" not in process.stdout
        )
    else:
        proof_valid = proof_path.exists() and proof_path.stat().st_size > 0
        if verify_process is not None:
            proof_valid = proof_valid and verify_process.returncode == 0
            proof_valid = proof_valid and verify_ok_text(case.protocol) in verify_process.stdout
        else:
            proof_valid = False

    if prove_ms is None:
        online_start = process.start_epoch
        online_end = process.end_epoch
    else:
        offset_s = ((program_ms or prove_ms) - prove_ms - (write_ms or 0.0)) / 1000.0
        online_start = process.start_epoch + max(0.0, offset_s)
        online_end = online_start + prove_ms / 1000.0
    note = ""
    if prove_ms is None:
        note = "missing online prove timing"
    return ProofRun(
        process=process,
        proof_path=proof_path,
        verify_process=verify_process,
        profile=profile,
        stark=stark,
        prove_ms=prove_ms,
        program_ms=program_ms,
        write_ms=write_ms,
        online_start=online_start,
        online_end=online_end,
        backend_ok=backend_ok,
        proof_valid=proof_valid,
        note=note,
    )


def _online_window(runs: list[ProofRun]) -> tuple[float, float, bool]:
    if not runs:
        return 0.0, 0.0, False
    starts = [run.online_start for run in runs]
    ends = [run.online_end for run in runs]
    start, end = max(starts), min(ends)
    overlap = end > start
    if not overlap:
        start, end = min(starts), max(ends)
    return start, end, overlap


def summarize_repetition(
    case: Case,
    repetition: int,
    runs: list[ProofRun],
    samples: list[Sample],
    *,
    trim_ratio: float,
    target: float,
    wave: int | None = None,
) -> RepetitionResult:
    result = RepetitionResult(case=case, repetition=repetition, runs=runs, samples=samples, wave=wave)
    if not runs:
        result.status = "failed"
        result.note = "no prove process"
        return result
    if any(run.process.timed_out for run in runs):
        result.status = "timeout"
    elif any(not run.process.ok for run in runs):
        result.status = "failed"
    elif any(not run.proof_valid for run in runs):
        result.status = "invalid"
    elif any(run.prove_ms is None for run in runs):
        result.status = "invalid"
        result.note = "missing prove timing"

    start, end, overlap = _online_window(runs)
    window_samples = [sample for sample in samples if start <= sample.timestamp <= end]
    devices = summarize_devices(
        window_samples, trim_ratio=trim_ratio, target=target
    )
    if not devices:
        result.util = {
            "util_mean": None,
            "util_p10": None,
            "util_min": None,
            "util_max": None,
            "util_ge90_ratio": None,
            "sample_count": 0,
            "window_start": start,
            "window_end": end,
            "window_overlap": overlap,
            "devices": {},
        }
        if result.status == "ok":
            result.status = "util_unknown"
    else:
        means = [item["util_mean"] for item in devices.values() if item["util_mean"] is not None]
        p10s = [item["util_p10"] for item in devices.values() if item["util_p10"] is not None]
        mins = [item["util_min"] for item in devices.values() if item["util_min"] is not None]
        ratios = [
            item["util_ge_target_ratio"]
            for item in devices.values()
            if item["util_ge_target_ratio"] is not None
        ]
        result.util = {
            "util_mean": statistics.fmean(means) if means else None,
            "util_p10": min(p10s) if p10s else None,
            "util_min": min(mins) if mins else None,
            "util_max": max(
                (
                    item["util_max"]
                    for item in devices.values()
                    if item["util_max"] is not None
                ),
                default=None,
            ),
            "util_ge90_ratio": min(ratios) if ratios else None,
            "sample_count": sum(item["sample_count"] for item in devices.values()),
            "window_start": start,
            "window_end": end,
            "window_overlap": overlap,
            "devices": devices,
        }
        if target is not None and result.util["util_mean"] is not None:
            if result.util["util_mean"] < target * 100.0 and result.status == "ok":
                result.status = "util_unqualified"
    if not overlap:
        result.note = (result.note + " no online-window overlap").strip()
    return result


def aggregate_repetitions(
    case: Case,
    repetitions: list[RepetitionResult],
    *,
    cpu_reference_ms: float | None = None,
) -> dict[str, Any]:
    measured = [item for item in repetitions if item.status in ("ok", "util_unqualified")]
    if not measured:
        measured = repetitions
    t_max_values = [item.t_max_ms for item in measured if item.t_max_ms is not None]
    throughput_values = [
        item.throughput_proofs_s
        for item in measured
        if item.throughput_proofs_s is not None
    ]
    util_values = [
        item.util.get("util_mean")
        for item in measured
        if item.util.get("util_mean") is not None
    ]
    status = "ok"
    if any(item.status == "failed" for item in repetitions):
        status = "failed"
    elif any(item.status == "timeout" for item in repetitions):
        status = "timeout"
    elif any(item.status == "invalid" for item in repetitions):
        status = "invalid"
    elif any(item.status == "util_unknown" for item in repetitions):
        status = "util_unknown"
    elif any(item.status == "util_unqualified" for item in repetitions):
        status = "util_unqualified"

    t_max_ms = statistics.median(t_max_values) if t_max_values else None
    throughput = statistics.median(throughput_values) if throughput_values else None
    util_mean = statistics.fmean(util_values) if util_values else None
    speedup = None
    measured_proofs = measured[0].m if measured else case.devices * case.concurrency
    if cpu_reference_ms and t_max_ms and t_max_ms > 0:
        speedup = measured_proofs * cpu_reference_ms / t_max_ms

    return {
        "case_id": case.case_id,
        "suite": case.suite,
        "protocol": case.protocol,
        "circuit": case.circuit,
        "density": case.density,
        "k": case.k,
        "constraints": None if case.k is None else 1 << case.k,
        "platform": case.platform,
        "display_platform": case.display_platform,
        "deployment": case.deployment,
        "nodes": len(case.nodes),
        "devices": case.devices,
        "concurrency": case.concurrency,
        "repeats": len(measured),
        "proofs": measured[0].m if measured else 0,
        "t_max_ms": t_max_ms,
        "t_median_ms": statistics.median(
            [item.t_median_ms for item in measured if item.t_median_ms is not None]
        )
        if any(item.t_median_ms is not None for item in measured)
        else None,
        "throughput_proofs_s": throughput,
        "throughput_constraints_s": None
        if throughput is None or case.k is None
        else throughput * (1 << case.k),
        "cpu_ref_ms": cpu_reference_ms,
        "speedup": speedup,
        "util_mean": util_mean,
        "util_p10": min(
            (
                item.util.get("util_p10")
                for item in measured
                if item.util.get("util_p10") is not None
            ),
            default=None,
        ),
        "util_min": min(
            (
                item.util.get("util_min")
                for item in measured
                if item.util.get("util_min") is not None
            ),
            default=None,
        ),
        "util_ge90_ratio": min(
            (
                item.util.get("util_ge90_ratio")
                for item in measured
                if item.util.get("util_ge90_ratio") is not None
            ),
            default=None,
        ),
        "status": status,
        "note": "; ".join(item.note for item in measured if item.note),
        "repetitions": [repetition_record(item) for item in repetitions],
    }


def repetition_record(item: RepetitionResult) -> dict[str, Any]:
    return {
        "repetition": item.repetition,
        "wave": item.wave,
        "m": item.m,
        "t_max_ms": item.t_max_ms,
        "t_median_ms": item.t_median_ms,
        "throughput_proofs_s": item.throughput_proofs_s,
        "util": item.util,
        "status": item.status,
        "note": item.note,
        "runs": [
            {
                "duration_s": run.process.duration_s,
                "returncode": run.process.returncode,
                "prove_ms": run.prove_ms,
                "program_ms": run.program_ms,
                "backend_ok": run.backend_ok,
                "proof_valid": run.proof_valid,
                "proof_path": str(run.proof_path),
            }
            for run in item.runs
        ],
    }


def write_cases_csv(path: Path, rows: list[dict[str, Any]]) -> None:
    ensure_dir(path.parent)
    fieldnames = [
        "case_id",
        "suite",
        "protocol",
        "circuit",
        "density",
        "k",
        "constraints",
        "platform",
        "display_platform",
        "deployment",
        "nodes",
        "devices",
        "concurrency",
        "repeats",
        "proofs",
        "waves",
        "proof_bits",
        "equivalent_bits",
        "t_max_ms",
        "t_median_ms",
        "throughput_proofs_s",
        "throughput_constraints_s",
        "throughput_equiv_2_30_s",
        "cpu_ref_ms",
        "speedup",
        "util_mean",
        "util_p10",
        "util_min",
        "util_ge90_ratio",
        "status",
        "note",
    ]
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames, extrasaction="ignore")
        writer.writeheader()
        for row in rows:
            writer.writerow(row)
