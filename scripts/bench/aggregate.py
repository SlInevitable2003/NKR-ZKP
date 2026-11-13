"""Aggregate per-node P3 results produced by Slurm jobs."""

from __future__ import annotations

import statistics
from pathlib import Path
from typing import Any

from .report import write_report
from .util import ensure_dir, read_json, write_json


def _wave_map(case: dict[str, Any]) -> dict[int, float]:
    result: dict[int, float] = {}
    for record in case.get("wave_records", []) or []:
        wave = record.get("wave")
        value = record.get("t_max_ms")
        if wave is not None and value is not None:
            result[int(wave)] = float(value)
    return result


def aggregate_nodes(base_dir: Path) -> dict[str, Any]:
    nodes_dir = base_dir / "nodes"
    if not nodes_dir.exists():
        raise RuntimeError(f"no per-node results under {nodes_dir}")

    node_meta: dict[str, dict[str, Any]] = {}
    groups: dict[str, dict[str, dict[str, Any]]] = {}
    for node_dir in sorted(path for path in nodes_dir.iterdir() if path.is_dir()):
        meta = read_json(node_dir / "node.json", default={})
        node_meta[node_dir.name] = meta if isinstance(meta, dict) else {}
        for case_path in sorted((node_dir / "cases").glob("*.json")):
            case = read_json(case_path, default=None)
            if not isinstance(case, dict) or not case.get("case_id"):
                continue
            groups.setdefault(str(case["case_id"]), {})[node_dir.name] = case

    aggregate_dir = ensure_dir(base_dir / "cases")
    summaries: list[dict[str, Any]] = []
    for case_id, per_node in sorted(groups.items()):
        node_names = sorted(per_node)
        first = per_node[node_names[0]]
        wave_maps = {node: _wave_map(per_node[node]) for node in node_names}
        wave_ids = sorted({wave for mapping in wave_maps.values() for wave in mapping})
        wave_records: list[dict[str, Any]] = []
        t_total_ms = 0.0
        for wave in wave_ids:
            values = [wave_maps[node].get(wave) for node in node_names]
            values = [value for value in values if value is not None]
            if not values:
                continue
            wave_t = max(values)
            t_total_ms += wave_t
            wave_records.append({"wave": wave, "t_max_ms": wave_t, "nodes": len(values)})
        if not wave_ids:
            values = [per_node[node].get("t_max_ms") for node in node_names]
            values = [float(value) for value in values if value is not None]
            if values:
                t_total_ms = max(values)

        devices_per_node = [
            int(node_meta.get(node, {}).get("devices", per_node[node].get("devices", 1)))
            for node in node_names
        ]
        total_devices = sum(devices_per_node)
        equivalent_proofs = total_devices
        equivalent_bits = int(first.get("equivalent_bits", 30))
        throughput = None if t_total_ms <= 0 else equivalent_proofs / (t_total_ms / 1000.0)
        constraints = (
            None if throughput is None else throughput * (1 << equivalent_bits)
        )
        cpu_refs = [
            float(per_node[node]["cpu_ref_ms"])
            for node in node_names
            if per_node[node].get("cpu_ref_ms") is not None
        ]
        cpu_ref = statistics.median(cpu_refs) if cpu_refs else None
        speedup = (
            None
            if not cpu_ref or t_total_ms <= 0
            else equivalent_proofs * cpu_ref / t_total_ms
        )
        utils = [
            float(per_node[node]["util_mean"])
            for node in node_names
            if per_node[node].get("util_mean") is not None
        ]
        util_mean = statistics.fmean(utils) if utils else None
        util_min_values = [
            float(per_node[node]["util_min"])
            for node in node_names
            if per_node[node].get("util_min") is not None
        ]
        util_ge_values = [
            float(per_node[node]["util_ge90_ratio"])
            for node in node_names
            if per_node[node].get("util_ge90_ratio") is not None
        ]

        status = "ok"
        statuses = [str(per_node[node].get("status", "failed")) for node in node_names]
        if any(value in ("failed", "timeout") for value in statuses):
            status = "failed"
        elif any(value == "invalid" for value in statuses):
            status = "invalid"
        elif any(value == "util_unknown" for value in statuses):
            status = "util_unknown"
        elif any(value == "util_unqualified" for value in statuses):
            status = "util_unqualified"

        summary = {
            "case_id": case_id,
            "suite": "p3",
            "protocol": first.get("protocol"),
            "circuit": first.get("circuit"),
            "density": first.get("density"),
            "k": first.get("k"),
            "constraints": first.get("constraints"),
            "platform": first.get("platform"),
            "display_platform": first.get("display_platform"),
            "deployment": first.get("deployment"),
            "nodes": len(node_names),
            "devices": devices_per_node[0] if devices_per_node else 1,
            "total_devices": total_devices,
            "concurrency": 1,
            "repeats": len(wave_ids),
            "proofs": sum(int(per_node[node].get("proofs", 0)) for node in node_names),
            "waves": len(wave_ids),
            "proof_bits": first.get("proof_bits"),
            "equivalent_bits": equivalent_bits,
            "t_max_ms": t_total_ms,
            "t_median_ms": statistics.median(
                [item["t_max_ms"] for item in wave_records]
            )
            if wave_records
            else None,
            "throughput_proofs_s": throughput,
            "throughput_constraints_s": constraints,
            "throughput_equiv_2_30_s": throughput,
            "cpu_ref_ms": cpu_ref,
            "speedup": speedup,
            "util_mean": util_mean,
            "util_p10": min(
                (
                    float(per_node[node]["util_p10"])
                    for node in node_names
                    if per_node[node].get("util_p10") is not None
                ),
                default=None,
            ),
            "util_min": min(util_min_values) if util_min_values else None,
            "util_ge90_ratio": min(util_ge_values) if util_ge_values else None,
            "status": status,
            "note": "",
            "wave_records": wave_records,
            "nodes_detail": node_names,
        }
        summaries.append(summary)
        write_json(aggregate_dir / f"{case_id}.json", summary)

    write_json(base_dir / "aggregate.json", {"nodes": sorted(node_meta), "cases": summaries})
    write_report(base_dir)
    return {"cases": len(summaries), "report": str(base_dir / "report.md")}
