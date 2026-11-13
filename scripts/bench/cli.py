"""Command-line entry point for the NKR-ZKP benchmark harness."""

from __future__ import annotations

import argparse
import os
import shutil
import sys
from pathlib import Path
from typing import Any

from .cases import Case, expand_suite, filter_tier, sort_cases
from .config import Config
from .qualify import qualify_cases
from .report import write_report
from .runner import CaseRunner
from .util import (
    REPO_ROOT,
    ensure_dir,
    environment,
    now_run_id,
    parse_bits,
    parse_csv,
    read_json,
    write_json,
)


def _add_config(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--config", type=Path, default=None, help="matrix.json path")


def _add_selection(parser: argparse.ArgumentParser) -> None:
    parser.add_argument(
        "--suite",
        choices=["functional", "p1", "p2", "p3", "all"],
        default="functional",
    )
    parser.add_argument(
        "--tier", choices=["smoke", "standard", "full"], default="full"
    )
    parser.add_argument("--circuit", default=None, help="comma-separated circuit filter")
    parser.add_argument("--density", default=None, help="comma-separated density filter")
    parser.add_argument("--k", default=None, help="e.g. 20,22,24 or 20-26")
    parser.add_argument("--platform", default=None, help="cpu,cuda,hip")
    parser.add_argument("--deployment", default=None)
    parser.add_argument("--protocol", default=None, help="groth16,plonk,stark")
    parser.add_argument("--limit", type=int, default=None)


def _select_cases(config: Config, args: argparse.Namespace) -> list[Case]:
    cases = expand_suite(config, args.suite)
    cases = filter_tier(cases, args.tier, config)
    if args.circuit:
        wanted = set(parse_csv(args.circuit))
        cases = [case for case in cases if case.circuit in wanted]
    if args.density:
        wanted = set(parse_csv(args.density))
        cases = [case for case in cases if case.density in wanted]
    if args.k:
        wanted = set(parse_bits(args.k))
        cases = [case for case in cases if case.k in wanted]
    if args.platform:
        wanted = set(parse_csv(args.platform))
        cases = [case for case in cases if case.platform in wanted]
    if args.deployment:
        wanted = set(parse_csv(args.deployment))
        cases = [case for case in cases if case.deployment in wanted]
    if args.protocol:
        wanted = set(parse_csv(args.protocol))
        cases = [case for case in cases if case.protocol in wanted]
    cases = sort_cases(cases)
    if args.limit:
        cases = cases[: args.limit]
    return cases


def _print_cases(cases: list[Case]) -> None:
    print(f"{len(cases)} case(s)")
    for case in cases:
        scale = f"2^{case.k}" if case.k is not None else f"n={case.param}"
        sim = f" {case.stark_profile}" if case.stark_profile else ""
        print(
            f"  {case.case_id:50s} {case.protocol:8s} {case.circuit:10s} "
            f"{case.density:7s} {scale:8s} {case.display_platform:9s} "
            f"{case.deployment}{sim}"
        )


def cmd_list(args: argparse.Namespace) -> int:
    config = Config.load(args.config)
    config.validate()
    _print_cases(_select_cases(config, args))
    return 0


def _required_commands(cases: list[Case], config: Config) -> list[Path]:
    commands: list[Path] = []
    seen: set[str] = set()
    for case in cases:
        if case.is_stark:
            candidate = config.bin_for("stark", "cpu")
            if str(candidate) not in seen:
                commands.append(candidate)
                seen.add(str(candidate))
            continue
        for stage in ("gen", "setup", "prove", "verify"):
            candidate = config.bin_for(case.protocol, case.platform) / f"{case.protocol}-{stage}"
            if str(candidate) not in seen:
                commands.append(candidate)
                seen.add(str(candidate))
    return commands


def cmd_preflight(args: argparse.Namespace) -> int:
    config = Config.load(args.config)
    config.validate()
    cases = _select_cases(config, args)
    problems: list[str] = []
    warnings: list[str] = []
    local_cases = [
        case
        for case in cases
        if any(config.node_is_local(node) for node in case.nodes)
    ]
    for command in _required_commands(local_cases, config):
        if not command.exists():
            problems.append(f"missing binary: {command}")
        elif not os.access(command, os.X_OK):
            problems.append(f"not executable: {command}")
    if args.remote:
        from .remote import check_node, node_from_config

        nodes = sorted({node for case in cases for node in case.nodes if not config.node_is_local(node)})
        remote_log = config.results_dir() / "_preflight"
        for node_name in nodes:
            node = node_from_config(config, node_name)
            ok, error = check_node(node, [], log_dir=remote_log)
            if ok:
                print(f"remote: {node_name} reachable")
            else:
                problems.append(f"remote {node_name} preflight failed: {error.strip()}")
    if not args.remote:
        for case in local_cases:
            if case.is_stark:
                continue
            for stage, flag in (("prove", "--output"), ("verify", "--proof")):
                binary = config.bin_for(case.protocol, case.platform) / f"{case.protocol}-{stage}"
                if not binary.exists():
                    continue
                try:
                    import subprocess

                    probe = subprocess.run(
                        [str(binary), "--help"],
                        capture_output=True,
                        text=True,
                        timeout=10,
                        check=False,
                    )
                    text = probe.stdout + probe.stderr
                    if flag not in text:
                        problems.append(
                            f"{binary} does not advertise {flag}; rebuild the app entry points"
                        )
                except (OSError, subprocess.SubprocessError) as exc:
                    warnings.append(f"could not probe {binary}: {exc}")
    for tool in ("nvidia-smi", "rocm-smi", "amd-smi"):
        if shutil.which(tool):
            print(f"tool: {tool} = {shutil.which(tool)}")
        else:
            warnings.append(f"tool not found: {tool}")
    print(f"cases: {len(cases)}")
    for warning in warnings:
        print(f"warning: {warning}")
    for problem in problems:
        print(f"error: {problem}")
    if problems:
        return 1
    print("preflight: ok")
    return 0


def cmd_inputs(args: argparse.Namespace) -> int:
    config = Config.load(args.config)
    config.validate()
    cases = _select_cases(config, args)
    runner = CaseRunner(config, args.run_dir or config.results_dir() / "inputs")
    for case in cases:
        if case.is_stark:
            print(f"{case.case_id}: STARK has no generated input")
            continue
        artifact = runner.input_manager.ensure(case)
        if artifact:
            print(
                f"{case.case_id}: constraints={artifact.constraints} "
                f"variables={artifact.variables} -> {artifact.directory}"
            )
    return 0


def cmd_qualify(args: argparse.Namespace) -> int:
    config = Config.load(args.config)
    config.validate()
    cases = [
        case
        for case in _select_cases(config, args)
        if case.suite in ("p1", "p2") and case.platform != "cpu"
    ]
    if not cases:
        print("no p1/p2 accelerator cases to qualify")
        return 0
    run_dir = args.run_dir or config.results_dir() / (args.run_id or now_run_id())
    ensure_dir(run_dir)
    qualify_cases(
        config,
        cases,
        run_dir,
        max_concurrency=args.max_concurrency,
        target=args.target,
    )
    print(f"qualification written to {run_dir / 'qualification.json'}")
    return 0


def _write_run_metadata(config: Config, run_dir: Path, cases: list[Case]) -> None:
    payload = {
        "config_path": str(config.path) if config.path else None,
        "config": config.data,
        "cases": [case.case_id for case in cases],
    }
    write_json(run_dir / "run.json", payload)
    env = environment()
    try:
        import subprocess

        env["git_commit"] = subprocess.run(
            ["git", "-C", str(REPO_ROOT), "rev-parse", "HEAD"],
            capture_output=True,
            text=True,
            check=False,
        ).stdout.strip()
        env["git_status"] = subprocess.run(
            ["git", "-C", str(REPO_ROOT), "status", "--short"],
            capture_output=True,
            text=True,
            check=False,
        ).stdout.strip()
    except OSError:
        pass
    write_json(run_dir / "environment.json", env)


def cmd_run(args: argparse.Namespace) -> int:
    config = Config.load(args.config)
    config.validate()
    cases = _select_cases(config, args)
    if not cases:
        print("no cases selected")
        return 1
    run_dir = args.run_dir or config.results_dir() / (args.run_id or now_run_id())
    ensure_dir(run_dir)
    _write_run_metadata(config, run_dir, cases)
    _print_cases(cases)
    if args.dry_run:
        print("dry-run: no process was started")
        return 0
    runner = CaseRunner(
        config,
        run_dir,
        dry_run=False,
        skip_cpu_reference=args.skip_cpu_reference,
        force=args.force,
    )
    failures = 0
    for index, case in enumerate(cases, start=1):
        print(f"[{index}/{len(cases)}] {case.case_id}", flush=True)
        try:
            summary = runner.run_case(case, concurrency=args.concurrency)
        except Exception as exc:
            failures += 1
            error = {
                "case_id": case.case_id,
                "suite": case.suite,
                "protocol": case.protocol,
                "circuit": case.circuit,
                "density": case.density,
                "k": case.k,
                "platform": case.platform,
                "display_platform": case.display_platform,
                "deployment": case.deployment,
                "status": "failed",
                "note": str(exc),
            }
            write_json(run_dir / "cases" / f"{case.case_id}.json", error)
            print(f"  failed: {exc}", file=sys.stderr)
            if args.fail_fast:
                break
            continue
        if summary:
            print(
                f"  status={summary.get('status')} "
                f"t_max={summary.get('t_max_ms')} ms "
                f"util={summary.get('util_mean')}",
                flush=True,
            )
            if summary.get("status") not in ("ok",):
                failures += 1
            if summary.get("status") == "failed" and args.fail_fast:
                break
    write_report(run_dir)
    print(f"results: {run_dir}")
    return 0 if failures == 0 else 2


def cmd_worker(args: argparse.Namespace) -> int:
    """Remote worker: prepare one node or run one P3 wave."""
    import time

    from .cases import Case

    plan = read_json(args.plan, default=None)
    if not isinstance(plan, dict):
        print(f"invalid worker plan: {args.plan}", file=sys.stderr)
        return 1
    start_at = plan.get("start_at")
    if start_at:
        delay = float(start_at) - time.time()
        if delay > 0:
            time.sleep(delay)
    config = Config(plan["config"])
    case_data = dict(plan["case"])
    case_data["nodes"] = tuple(case_data.get("nodes", ["local"]))
    case = Case(**case_data)
    run_dir = ensure_dir(Path(plan["run_dir"]))
    result_path = Path(plan["result_path"])
    runner = CaseRunner(config, run_dir, force=True)
    result: dict[str, Any] = {"mode": plan.get("mode"), "node": plan.get("node")}
    try:
        runner.prepare(case)
        if plan.get("mode") == "setup":
            result.update({"status": "ok"})
        else:
            wave = int(plan.get("wave", 1))
            repetition = runner._run_once(case, 1, wave - 1, wave=wave)  # noqa: SLF001
            result.update(
                {
                    "status": repetition.status,
                    "wave": wave,
                    "t_max_ms": repetition.t_max_ms,
                    "t_median_ms": repetition.t_median_ms,
                    "throughput_proofs_s": repetition.throughput_proofs_s,
                    "util": repetition.util,
                    "note": repetition.note,
                    "runs": [
                        {
                            "prove_ms": run.prove_ms,
                            "proof_valid": run.proof_valid,
                            "backend_ok": run.backend_ok,
                        }
                        for run in repetition.runs
                    ],
                }
            )
    except Exception as exc:
        result.update({"status": "failed", "note": str(exc)})
    write_json(result_path, result)
    return 0 if result.get("status") in ("ok", "util_unqualified") else 1


def cmd_report(args: argparse.Namespace) -> int:
    config = Config.load(args.config)
    run_dir = args.run_dir or config.results_dir() / args.run_id
    if not run_dir.exists():
        print(f"run directory not found: {run_dir}", file=sys.stderr)
        return 1
    result = write_report(run_dir)
    print(f"report: {result['report']} ({result['cases']} cases)")
    return 0


def cmd_clean(args: argparse.Namespace) -> int:
    config = Config.load(args.config)
    run_dir = (args.run_dir or config.results_dir() / args.run_id).resolve()
    results_root = config.results_dir().resolve()
    if results_root not in run_dir.parents or run_dir == results_root:
        print(f"refusing to remove outside results dir: {run_dir}", file=sys.stderr)
        return 1
    if not run_dir.exists():
        print(f"not found: {run_dir}")
        return 0
    removed = 0
    for path in sorted(run_dir.rglob("*"), reverse=True):
        if path.is_dir():
            try:
                path.rmdir()
            except OSError:
                pass
        else:
            path.unlink(missing_ok=True)
            removed += 1
    try:
        run_dir.rmdir()
    except OSError:
        pass
    print(f"removed {removed} file(s) under {run_dir}")
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="python3 -m scripts.bench",
        description="NKR-ZKP benchmark harness",
    )
    sub = parser.add_subparsers(dest="command", required=True)

    list_parser = sub.add_parser("list", help="list selected cases")
    _add_config(list_parser)
    _add_selection(list_parser)
    list_parser.set_defaults(func=cmd_list)

    preflight_parser = sub.add_parser("preflight", help="check binaries and tools")
    _add_config(preflight_parser)
    _add_selection(preflight_parser)
    preflight_parser.add_argument("--remote", action="store_true")
    preflight_parser.set_defaults(func=cmd_preflight)

    inputs_parser = sub.add_parser("inputs", help="generate/copy circuit inputs")
    _add_config(inputs_parser)
    _add_selection(inputs_parser)
    inputs_parser.add_argument("--run-dir", type=Path, default=None)
    inputs_parser.set_defaults(func=cmd_inputs)

    qualify_parser = sub.add_parser("qualify", help="calibrate concurrency to 90%% utilization")
    _add_config(qualify_parser)
    _add_selection(qualify_parser)
    qualify_parser.add_argument("--run-id", default=None)
    qualify_parser.add_argument("--run-dir", type=Path, default=None)
    qualify_parser.add_argument("--max-concurrency", type=int, default=None)
    qualify_parser.add_argument("--target", type=float, default=None)
    qualify_parser.set_defaults(func=cmd_qualify)

    run_parser = sub.add_parser("run", help="run selected cases")
    _add_config(run_parser)
    _add_selection(run_parser)
    run_parser.add_argument("--run-id", default=None)
    run_parser.add_argument("--run-dir", type=Path, default=None)
    run_parser.add_argument("--dry-run", action="store_true")
    run_parser.add_argument("--force", action="store_true")
    run_parser.add_argument("--skip-cpu-reference", action="store_true")
    run_parser.add_argument("--concurrency", type=int, default=None)
    run_parser.add_argument("--fail-fast", action="store_true")
    run_parser.set_defaults(func=cmd_run)

    report_parser = sub.add_parser("report", help="write report.md from a run")
    _add_config(report_parser)
    report_parser.add_argument("--run-id", required=True)
    report_parser.add_argument("--run-dir", type=Path, default=None)
    report_parser.set_defaults(func=cmd_report)

    clean_parser = sub.add_parser("clean", help="remove a run directory")
    _add_config(clean_parser)
    clean_parser.add_argument("--run-id", required=True)
    clean_parser.add_argument("--run-dir", type=Path, default=None)
    clean_parser.set_defaults(func=cmd_clean)

    worker_parser = sub.add_parser("_worker", help="internal remote worker")
    worker_parser.add_argument("--plan", type=Path, required=True)
    worker_parser.set_defaults(func=cmd_worker)

    return parser


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    if getattr(args, "fail_fast", False):
        args.fail_fast = True
    try:
        return int(args.func(args))
    except BrokenPipeError:
        return 0


if __name__ == "__main__":
    raise SystemExit(main())
