"""Case orchestration: prepare, prove, verify, aggregate."""

from __future__ import annotations

import statistics
import time
from dataclasses import replace
from pathlib import Path
from typing import Any

from .cases import Case
from .config import Config
from .executor import ProcessResult, run_parallel, run_process
from .inputs import InputArtifact, InputManager, SetupArtifact
from .metrics import (
    RepetitionResult,
    aggregate_repetitions,
    build_proof_run,
    summarize_repetition,
)
from .monitor import MonitorSet
from .protocols import (
    build_prove,
    build_verify,
    parse_profile,
    stark_command,
    stark_threads,
)
from .util import ensure_dir, read_json, write_json


class CaseRunner:
    def __init__(
        self,
        config: Config,
        run_dir: Path,
        *,
        dry_run: bool = False,
        skip_cpu_reference: bool = False,
        force: bool = False,
    ):
        self.config = config
        self.run_dir = ensure_dir(run_dir)
        self.dry_run = dry_run
        self.skip_cpu_reference = skip_cpu_reference
        self.force = force
        self.input_manager = InputManager(config)
        self.prepared: dict[str, tuple[InputArtifact | None, SetupArtifact | None]] = {}
        self.qualification = read_json(self.run_dir / "qualification.json", default={})
        self.results: dict[str, dict[str, Any]] = {}
        self.cases_dir = ensure_dir(self.run_dir / "cases")
        self._load_existing()

    def _load_existing(self) -> None:
        for path in sorted(self.cases_dir.glob("*.json")):
            data = read_json(path, default=None)
            if isinstance(data, dict) and data.get("case_id"):
                self.results[str(data["case_id"])] = data

    def workdir(self, case: Case) -> Path:
        return ensure_dir(self.run_dir / "work" / case.case_id)

    def prepare(self, case: Case) -> tuple[InputArtifact | None, SetupArtifact | None]:
        if case.case_id in self.prepared:
            return self.prepared[case.case_id]
        if self.dry_run:
            return None, None
        workdir = self.workdir(case)
        prepared = self.input_manager.prepare(case, workdir)
        self.prepared[case.case_id] = prepared
        return prepared

    def resolve_concurrency(self, case: Case) -> int:
        if case.platform == "cpu":
            return 1
        if case.suite == "p3":
            return 1
        deployment_value = self.config.deployment(case.deployment).get("concurrency", 1)
        if isinstance(deployment_value, int):
            return max(1, deployment_value)
        # auto
        entry = self.qualification.get(case.case_id)
        if isinstance(entry, dict) and entry.get("concurrency"):
            return max(1, int(entry["concurrency"]))
        if isinstance(entry, int):
            return max(1, entry)
        return 1

    def run_case(self, case: Case, *, concurrency: int | None = None) -> dict[str, Any] | None:
        if self.dry_run:
            return None
        if not self.force and self.results.get(case.case_id, {}).get("status") == "ok":
            return self.results[case.case_id]
        p = int(concurrency) if concurrency else self.resolve_concurrency(case)
        if case.suite == "p3":
            summary = self._run_p3(case)
        else:
            summary = self._run_standard(case, p)
        path = self.cases_dir / f"{case.case_id}.json"
        write_json(path, summary)
        if summary.get("status") != "ok":
            audit_dir = ensure_dir(self.run_dir / "audit")
            write_json(audit_dir / f"{case.case_id}.json", summary)
        self.results[case.case_id] = summary
        return summary

    def _run_standard(self, case: Case, p: int) -> dict[str, Any]:
        self.prepare(case)
        repetitions: list[RepetitionResult] = []
        total = case.warmup + case.repeats
        for rep in range(total):
            result = self._run_once(case, p, rep)
            repetitions.append(result)
            if result.status in ("failed", "timeout") and self.config.get(
                "run", "fail_fast", default=False
            ):
                break
        measured = repetitions[case.warmup :] if case.warmup else repetitions
        if not measured:
            measured = repetitions
        cpu_ref = None
        if case.cpu_reference and not self.skip_cpu_reference:
            cpu_ref = self.cpu_reference(case)
        return aggregate_repetitions(case, measured, cpu_reference_ms=cpu_ref)

    def _run_p3(self, case: Case) -> dict[str, Any]:
        if any(not self.config.node_is_local(node) for node in case.nodes):
            return self._run_p3_remote(case)
        self.prepare(case)
        waves: list[RepetitionResult] = []
        for wave in range(1, case.waves + 1):
            result = self._run_once(case, 1, wave - 1, wave=wave)
            result.wave = wave
            waves.append(result)
        measured = waves[case.warmup :] if case.warmup else waves
        if not measured:
            measured = waves
        cpu_ref_total = None
        if case.cpu_reference and not self.skip_cpu_reference:
            cpu_ref_total = self.cpu_reference(case, count=case.waves)
        t_total_ms = sum(
            item.t_max_ms for item in measured if item.t_max_ms is not None
        )
        equivalent = len(case.nodes) * case.devices
        throughput_equiv = (
            None if t_total_ms <= 0 else equivalent / (t_total_ms / 1000.0)
        )
        constraints = (
            None
            if throughput_equiv is None or case.equivalent_bits is None
            else throughput_equiv * (1 << case.equivalent_bits)
        )
        util_values = [
            item.util.get("util_mean")
            for item in measured
            if item.util.get("util_mean") is not None
        ]
        status = "ok"
        if any(item.status in ("failed", "timeout") for item in measured):
            status = "failed"
        elif any(item.status == "invalid" for item in measured):
            status = "invalid"
        elif any(item.status == "util_unknown" for item in measured):
            status = "util_unknown"
        elif any(item.status == "util_unqualified" for item in measured):
            status = "util_unqualified"
        speedup = (
            None
            if not cpu_ref_total or t_total_ms <= 0
            else equivalent * cpu_ref_total / t_total_ms
        )
        summary = {
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
            "concurrency": 1,
            "repeats": len(measured),
            "proofs": sum(item.m for item in measured),
            "waves": len(measured),
            "proof_bits": case.proof_bits,
            "equivalent_bits": case.equivalent_bits,
            "t_max_ms": t_total_ms,
            "t_median_ms": statistics.median(
                [item.t_max_ms for item in measured if item.t_max_ms is not None]
            )
            if any(item.t_max_ms is not None for item in measured)
            else None,
            "throughput_proofs_s": throughput_equiv,
            "throughput_constraints_s": constraints,
            "throughput_equiv_2_30_s": throughput_equiv,
            "cpu_ref_ms": cpu_ref_total,
            "speedup": speedup,
            "util_mean": statistics.fmean(util_values) if util_values else None,
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
            "wave_records": [
                {
                    "wave": item.wave,
                    "t_max_ms": item.t_max_ms,
                    "util": item.util,
                    "status": item.status,
                    "runs": [
                        {
                            "prove_ms": run.prove_ms,
                            "proof_valid": run.proof_valid,
                            "backend_ok": run.backend_ok,
                        }
                        for run in item.runs
                    ],
                }
                for item in measured
            ],
        }
        return summary

    def _run_p3_remote(self, case: Case) -> dict[str, Any]:
        from concurrent.futures import ThreadPoolExecutor
        from dataclasses import asdict

        from .remote import (
            copy_from,
            copy_to,
            ensure_remote_dir,
            node_from_config,
            run_worker,
            write_plan,
        )
        from .util import read_json

        nodes = [node_from_config(self.config, name) for name in case.nodes]
        local_root = ensure_dir(self.run_dir / "remote")
        run_name = self.run_dir.name
        case_plan = asdict(case)
        case_plan["nodes"] = ["local"]
        config_data = self.config.data
        timeout_s = int(self.config.get("run", "timeout_s", default=7200))

        def remote_dir(node):
            return node.workdir / "nkr-runs" / run_name / case.case_id

        def setup_node(node):
            node_root = ensure_dir(local_root / node.name)
            plan = {
                "mode": "setup",
                "node": node.name,
                "case": case_plan,
                "config": config_data,
                "run_dir": str(remote_dir(node)),
                "result_path": str(remote_dir(node) / "setup.result.json"),
                "start_at": time.time(),
            }
            plan_local = node_root / "setup.plan.json"
            write_plan(plan_local, plan)
            if not ensure_remote_dir(node, remote_dir(node), node_root, f"{node.name}-mkdir-setup"):
                return node.name, False, "mkdir failed"
            if not copy_to(
                node,
                plan_local,
                remote_dir(node) / "setup.plan.json",
                log_dir=node_root,
                name=f"{node.name}-copy-setup-plan",
            ):
                return node.name, False, "copy plan failed"
            process = run_worker(
                node,
                plan_path=remote_dir(node) / "setup.plan.json",
                log_dir=node_root,
                name=f"{node.name}-setup",
                timeout_s=timeout_s,
            )
            if not process.ok:
                return node.name, False, f"setup worker failed: {process.returncode}"
            if not copy_from(
                node,
                remote_dir(node) / "setup.result.json",
                node_root / "setup.result.json",
                log_dir=node_root,
                name=f"{node.name}-copy-setup-result",
            ):
                return node.name, False, "copy setup result failed"
            result = read_json(node_root / "setup.result.json", default={})
            return node.name, bool(result.get("status") == "ok"), str(result.get("note", ""))

        setup_results: list[tuple[str, bool, str]] = []
        with ThreadPoolExecutor(max_workers=len(nodes)) as pool:
            for item in pool.map(setup_node, nodes):
                setup_results.append(item)
        if not all(item[1] for item in setup_results):
            failed = ", ".join(f"{name}: {note}" for name, ok, note in setup_results if not ok)
            return self._failed_p3_summary(case, f"remote setup failed: {failed}")

        wave_records: list[dict[str, Any]] = []
        try:
            for wave in range(1, case.waves + 1):
                start_at = time.time() + 10.0
                plans: dict[str, Any] = {}
                node_roots: dict[str, Path] = {}
                for node in nodes:
                    node_root = ensure_dir(local_root / node.name / f"wave{wave:02d}")
                    node_roots[node.name] = node_root
                    plan = {
                        "mode": "wave",
                        "node": node.name,
                        "wave": wave,
                        "case": case_plan,
                        "config": config_data,
                        "run_dir": str(remote_dir(node)),
                        "result_path": str(remote_dir(node) / f"wave{wave:02d}.result.json"),
                        "start_at": start_at,
                    }
                    plan_local = node_root / "wave.plan.json"
                    write_plan(plan_local, plan)
                    plans[node.name] = plan
                    if not copy_to(
                        node,
                        plan_local,
                        remote_dir(node) / f"wave{wave:02d}.plan.json",
                        log_dir=node_root,
                        name=f"{node.name}-copy-wave-plan",
                    ):
                        raise RuntimeError(f"failed to copy plan to {node.name}")
                with ThreadPoolExecutor(max_workers=len(nodes)) as pool:
                    futures = [
                        pool.submit(
                            run_worker,
                            node,
                            plan_path=remote_dir(node) / f"wave{wave:02d}.plan.json",
                            log_dir=node_roots[node.name],
                            name=f"{node.name}-wave{wave:02d}",
                            timeout_s=timeout_s,
                        )
                        for node in nodes
                    ]
                    worker_results = [future.result() for future in futures]
                node_wave: list[dict[str, Any]] = []
                for node, process in zip(nodes, worker_results):
                    node_root = node_roots[node.name]
                    if not process.ok:
                        raise RuntimeError(f"worker {node.name} wave {wave} failed")
                    if not copy_from(
                        node,
                        remote_dir(node) / f"wave{wave:02d}.result.json",
                        node_root / "wave.result.json",
                        log_dir=node_root,
                        name=f"{node.name}-copy-wave-result",
                    ):
                        raise RuntimeError(f"failed to copy result from {node.name}")
                    node_wave.append(
                        read_json(node_root / "wave.result.json", default={})
                    )
                wave_records.append({"wave": wave, "nodes": node_wave})
        except Exception as exc:
            return self._failed_p3_summary(case, f"remote wave failed: {exc}")

        t_total_ms = 0.0
        util_values: list[float] = []
        status = "ok"
        wave_summaries: list[dict[str, Any]] = []
        for record in wave_records:
            entries = record["nodes"]
            values = [item.get("t_max_ms") for item in entries]
            values = [value for value in values if value is not None]
            if len(values) != len(entries):
                status = "invalid"
                continue
            wave_t = max(values)
            t_total_ms += wave_t
            util = [item.get("util", {}).get("util_mean") for item in entries]
            util = [value for value in util if value is not None]
            if util:
                util_values.extend(util)
            if any(item.get("status") not in ("ok", "util_unqualified") for item in entries):
                status = "failed"
            if any(item.get("status") == "util_unqualified" for item in entries) and status == "ok":
                status = "util_unqualified"
            wave_summaries.append(
                {
                    "wave": record["wave"],
                    "t_max_ms": wave_t,
                    "status": "ok"
                    if all(item.get("status") in ("ok", "util_unqualified") for item in entries)
                    else "failed",
                    "nodes": entries,
                }
            )

        cpu_ref_total = None
        if case.cpu_reference and not self.skip_cpu_reference:
            cpu_ref_total = self.cpu_reference(case, count=case.waves)
        equivalent = len(case.nodes) * case.devices
        throughput = None if t_total_ms <= 0 else equivalent / (t_total_ms / 1000.0)
        constraints = (
            None
            if throughput is None or case.equivalent_bits is None
            else throughput * (1 << case.equivalent_bits)
        )
        speedup = (
            None
            if not cpu_ref_total or t_total_ms <= 0
            else equivalent * cpu_ref_total / t_total_ms
        )
        util_mean = statistics.fmean(util_values) if util_values else None
        if (
            status == "ok"
            and util_mean is not None
            and util_mean < float(self.config.get("utilization", "target", default=0.90)) * 100.0
        ):
            status = "util_unqualified"
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
            "concurrency": 1,
            "repeats": len(wave_summaries),
            "proofs": len(wave_summaries) * case.devices * len(case.nodes),
            "waves": len(wave_summaries),
            "proof_bits": case.proof_bits,
            "equivalent_bits": case.equivalent_bits,
            "t_max_ms": t_total_ms,
            "t_median_ms": statistics.median(
                [item["t_max_ms"] for item in wave_summaries if item.get("t_max_ms")]
            )
            if wave_summaries
            else None,
            "throughput_proofs_s": throughput,
            "throughput_constraints_s": constraints,
            "throughput_equiv_2_30_s": throughput,
            "cpu_ref_ms": cpu_ref_total,
            "speedup": speedup,
            "util_mean": util_mean,
            "util_p10": min(
                (
                    item.get("util", {}).get("util_p10")
                    for record in wave_records
                    for item in record["nodes"]
                    if item.get("util", {}).get("util_p10") is not None
                ),
                default=None,
            ),
            "util_min": min(
                (
                    item.get("util", {}).get("util_min")
                    for record in wave_records
                    for item in record["nodes"]
                    if item.get("util", {}).get("util_min") is not None
                ),
                default=None,
            ),
            "util_ge90_ratio": min(
                (
                    item.get("util", {}).get("util_ge90_ratio")
                    for record in wave_records
                    for item in record["nodes"]
                    if item.get("util", {}).get("util_ge90_ratio") is not None
                ),
                default=None,
            ),
            "status": status,
            "note": "",
            "wave_records": wave_summaries,
        }

    def _failed_p3_summary(self, case: Case, note: str) -> dict[str, Any]:
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
            "concurrency": 1,
            "repeats": 0,
            "proofs": 0,
            "waves": 0,
            "proof_bits": case.proof_bits,
            "equivalent_bits": case.equivalent_bits,
            "t_max_ms": None,
            "t_median_ms": None,
            "throughput_proofs_s": None,
            "throughput_constraints_s": None,
            "throughput_equiv_2_30_s": None,
            "cpu_ref_ms": None,
            "speedup": None,
            "util_mean": None,
            "util_p10": None,
            "util_min": None,
            "util_ge90_ratio": None,
            "status": "failed",
            "note": note,
            "wave_records": [],
        }

    def _run_once(
        self,
        case: Case,
        p: int,
        repetition: int,
        *,
        wave: int | None = None,
    ) -> RepetitionResult:
        if case.is_stark:
            return self._run_stark_once(case, repetition, wave=wave)
        workdir = self.workdir(case)
        proof_dir = ensure_dir(workdir / "proofs" / f"rep{repetition:03d}")
        devices = max(1, case.devices)
        device_ids = self._device_ids(case)
        specs: list[dict[str, Any]] = []
        proof_paths: dict[int, Path] = {}
        for index in range(devices * p):
            device_index = index // p
            proof_path = proof_dir / f"p{index:03d}.proof.bin"
            proof_paths[index] = proof_path
            env = self._process_env(case, device_index, device_ids)
            argv = build_prove(
                self.config,
                case.protocol,
                case.platform,
                case,
                workdir,
                self._stem(case),
                proof_path,
            )
            specs.append(
                {
                    "argv": argv,
                    "log_dir": proof_dir,
                    "name": f"prove_p{index:03d}",
                    "env": env,
                    "cwd": self.config.repo_root,
                    "timeout_s": int(self.config.get("run", "timeout_s", default=7200)),
                }
            )

        monitor = MonitorSet(
            case.platform,
            host="local",
            device_ids=device_ids,
            interval_ms=int(self.config.get("utilization", "sample_ms", default=100)),
        )
        monitor.start()
        start_at = time.time() + 1.0
        processes = run_parallel(specs, start_at=start_at)
        samples = monitor.stop()
        self._write_samples(case, repetition, samples)

        verify_processes: dict[int, ProcessResult | None] = {}
        for index, process in enumerate(processes):
            if not process.ok:
                verify_processes[index] = None
                continue
            verify_processes[index] = run_process(
                build_verify(
                    self.config,
                    case.protocol,
                    case.platform,
                    case,
                    workdir,
                    self._stem(case),
                    proof_paths[index],
                ),
                log_dir=proof_dir,
                name=f"verify_p{index:03d}",
                cwd=self.config.repo_root,
                timeout_s=int(self.config.get("run", "timeout_s", default=7200)),
            )

        runs = [
            build_proof_run(
                case,
                processes[index],
                proof_paths[index],
                verify_processes.get(index),
            )
            for index in range(len(processes))
        ]
        result = summarize_repetition(
            case,
            repetition,
            runs,
            samples,
            trim_ratio=float(self.config.get("utilization", "trim_ratio", default=0.10)),
            target=self._util_target(case),
            wave=wave,
        )
        if monitor.errors:
            result.note = (result.note + " " + "; ".join(monitor.errors)).strip()
        return result

    def _run_stark_once(
        self, case: Case, repetition: int, *, wave: int | None = None
    ) -> RepetitionResult:
        workdir = self.workdir(case)
        log_dir = ensure_dir(workdir / "logs" / f"rep{repetition:03d}")
        threads = stark_threads(self.config, case)
        monitor = MonitorSet(
            "cpu",
            host="local",
            interval_ms=int(self.config.get("utilization", "sample_ms", default=100)),
        )
        monitor.start()
        process = run_process(
            stark_command(self.config, case, threads),
            log_dir=log_dir,
            name="stark",
            env={"RAYON_NUM_THREADS": str(threads), "OMP_NUM_THREADS": str(threads)},
            cwd=self.config.repo_root,
            timeout_s=int(self.config.get("run", "timeout_s", default=7200)),
        )
        samples = monitor.stop()
        self._write_samples(case, repetition, samples)
        dummy_proof = workdir / "proofs" / f"stark_rep{repetition:03d}.proof.bin"
        run = build_proof_run(case, process, dummy_proof, None)
        result = summarize_repetition(
            case,
            repetition,
            [run],
            samples,
            trim_ratio=float(self.config.get("utilization", "trim_ratio", default=0.10)),
            target=self._util_target(case),
            wave=wave,
        )
        if monitor.errors:
            result.note = (result.note + " " + "; ".join(monitor.errors)).strip()
        return result

    def _util_target(self, case: Case) -> float | None:
        if case.suite == "functional" or case.platform == "cpu":
            return None
        return float(self.config.get("utilization", "target", default=0.90))

    def _write_samples(self, case: Case, repetition: int, samples) -> None:
        from .monitor import write_samples_csv

        path = self.run_dir / "samples" / case.case_id / f"rep{repetition:03d}.csv"
        write_samples_csv(path, samples)

    def _stem(self, case: Case) -> str:
        from .cases import stem_for

        if case.param is None:
            raise ValueError("case has no parameter")
        return stem_for(case.circuit, case.param, case.density)

    def _device_ids(self, case: Case) -> list[str]:
        if case.platform == "cpu":
            return []
        node = self.config.node(case.nodes[0] if case.nodes else "local")
        uuids = node.get("device_uuids") or node.get("devices") or []
        if isinstance(uuids, list) and len(uuids) >= case.devices:
            return [str(item) for item in uuids[: case.devices]]
        return [str(index) for index in range(case.devices)]

    def _process_env(self, case: Case, device_index: int, device_ids: list[str]) -> dict[str, str]:
        env = {"OMP_NUM_THREADS": "1"}
        device = device_ids[device_index] if device_index < len(device_ids) else str(device_index)
        if case.platform == "cuda":
            env["CUDA_VISIBLE_DEVICES"] = device
        elif case.platform == "hip":
            env["HIP_VISIBLE_DEVICES"] = device
            env["ROCR_VISIBLE_DEVICES"] = device
        return env

    def cpu_reference(self, case: Case, *, count: int = 1) -> float | None:
        cache_path = self.run_dir / "cpu-reference" / f"{case.case_id}-x{count}.json"
        if cache_path.exists() and not self.force:
            data = read_json(cache_path, default={})
            if data.get("total_ms") is not None:
                return float(data["total_ms"])
        cpu_case = replace(
            case,
            platform="cpu",
            deployment="cpu",
            nodes=("local",),
            devices=1,
            concurrency=1,
            cpu_reference=False,
            suite=f"{case.suite}-cpu-ref",
        )
        workdir = ensure_dir(self.run_dir / "work" / f"{case.case_id}-cpu-ref")
        self.input_manager.prepare(cpu_case, workdir)
        total_ms = 0.0
        prove_ms: list[float] = []
        for index in range(count):
            proof_path = ensure_dir(workdir / "proofs") / f"cpu_ref_{index:03d}.proof.bin"
            argv = build_prove(
                self.config,
                "groth16",
                "cpu",
                cpu_case,
                workdir,
                self._stem(cpu_case),
                proof_path,
            )
            process = run_process(
                argv,
                log_dir=ensure_dir(workdir / "logs"),
                name=f"cpu_ref_{index:03d}",
                env={"OMP_NUM_THREADS": "1"},
                cwd=self.config.repo_root,
                timeout_s=int(self.config.get("run", "timeout_s", default=7200)),
            )
            if not process.ok:
                raise RuntimeError(
                    f"CPU reference failed for {case.case_id}: {process.returncode}\n"
                    f"{process.stderr[-4000:]}"
                )
            profile = parse_profile(process.stdout)
            value = profile.get("groth16.prove.total")
            if value is None:
                raise RuntimeError(f"CPU reference missing prove timing: {case.case_id}")
            prove_ms.append(value)
            total_ms += value
        data = {
            "case_id": case.case_id,
            "count": count,
            "total_ms": total_ms if count > 1 else prove_ms[0],
            "prove_ms": prove_ms,
            "median_ms": statistics.median(prove_ms),
        }
        write_json(cache_path, data)
        return float(data["total_ms"])
