"""Command-line entry point for the NKR-ZKP benchmark harness."""

from __future__ import annotations

import argparse
import os
import shutil
import sys
from pathlib import Path
from typing import Any

from .cases import Case, expand_suite, sort_cases
from .config import Config
from .report import write_report
from .runner import CaseRunner
from .util import (
    REPO_ROOT,
    ensure_dir,
    environment,
    now_run_id,
    parse_bits,
    parse_csv,
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
    parser.add_argument("--circuit", default=None, help="comma-separated circuit filter")
    parser.add_argument("--density", default=None, help="comma-separated density filter")
    parser.add_argument("--k", default=None, help="e.g. 20,22,24 or 20-26")
    parser.add_argument("--platform", default=None, help="cpu,cuda,hip")
    parser.add_argument("--deployment", default=None)
    parser.add_argument("--protocol", default=None, help="groth16,plonk,stark")
    parser.add_argument("--limit", type=int, default=None)


def _select_cases(config: Config, args: argparse.Namespace) -> list[Case]:
    cases = expand_suite(config, args.suite)
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
        if args.suite == "functional" and ("cuda" in wanted or "hip" in wanted):
            wanted.add("cpu")  # functional also covers CPU and STARK on the same host
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


def _run_environment_problems(cases: list[Case], config: Config) -> list[str]:
    problems: list[str] = []
    for command in _required_commands(cases, config):
        if not command.exists():
            problems.append(f"missing binary: {command}")
        elif not os.access(command, os.X_OK):
            problems.append(f"not executable: {command}")

    checked: set[tuple[str, str]] = set()
    for case in cases:
        if case.is_stark or case.param is None:
            continue
        key = (case.protocol, case.platform)
        if key in checked:
            continue
        checked.add(key)
        generator = config.bin_for(case.protocol, case.platform) / f"{case.protocol}-gen"
        if not generator.exists():
            continue
        try:
            import subprocess

            probe = subprocess.run(
                [str(generator), "--help"],
                capture_output=True,
                text=True,
                timeout=10,
                check=False,
            )
            text = probe.stdout + probe.stderr
            required_cases = ["matmul", "fibonacci", "poseidon", "sql-aggregate"]
            missing = [name for name in required_cases if name not in text]
            if "--density" not in text or missing:
                problems.append(
                    f"stale binary: {generator} does not advertise "
                    f"all circuits/density (missing={missing}); rebuild it"
                )
        except (OSError, subprocess.SubprocessError) as exc:
            problems.append(f"could not probe {generator}: {exc}")

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
                        f"stale binary: {binary} does not advertise {flag}; rebuild it"
                    )
            except (OSError, subprocess.SubprocessError) as exc:
                problems.append(f"could not probe {binary}: {exc}")
    return problems


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


def _write_run_metadata(config: Config, run_dir: Path, cases: list[Case]) -> None:
    write_json(
        run_dir / "run.json",
        {
            "config_path": str(config.path) if config.path else None,
            "config": config.data,
            "cases": [case.case_id for case in cases],
        },
    )
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
    if any(case.suite == "p3" for case in cases):
        print(
            "P3 is executed per node. Use `run-node --node <name> --deployment <name>` "
            "inside each Slurm job, then `aggregate` on the shared results directory.",
            file=sys.stderr,
        )
        return 1
    run_dir = args.run_dir or config.results_dir() / (args.run_id or now_run_id())
    ensure_dir(run_dir)
    _write_run_metadata(config, run_dir, cases)
    _print_cases(cases)
    if args.dry_run:
        print("dry-run: no process was started")
        return 0
    environment_problems = _run_environment_problems(cases, config)
    if environment_problems:
        for problem in environment_problems:
            print(f"error: {problem}", file=sys.stderr)
        print(
            "fix the build paths in scripts/configs/matrix.json or rebuild the binaries; "
            "for a CPU-only run use `--platform cpu`.",
            file=sys.stderr,
        )
        return 1
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


def cmd_run_node(args: argparse.Namespace) -> int:
    """Run this node's P3 share; Slurm starts one of these per node."""
    from dataclasses import replace

    config = Config.load(args.config)
    config.validate()
    if not args.deployment:
        print("run-node requires --deployment", file=sys.stderr)
        return 1
    selected = argparse.Namespace(**vars(args))
    selected.suite = "p3"
    cases = _select_cases(config, selected)
    cases = [case for case in cases if args.node in case.nodes]
    if not cases:
        print(f"no P3 cases for node {args.node} in {args.deployment}", file=sys.stderr)
        return 1
    environment_problems = _run_environment_problems(cases, config)
    if environment_problems:
        for problem in environment_problems:
            print(f"error: {problem}", file=sys.stderr)
        return 1
    base_dir = args.run_dir or config.results_dir() / (args.run_id or now_run_id())
    node_dir = ensure_dir(base_dir / "nodes" / args.node)
    runner = CaseRunner(
        config,
        node_dir,
        skip_cpu_reference=args.skip_cpu_reference,
        force=args.force,
    )
    results: list[dict[str, Any]] = []
    for index, case in enumerate(cases, start=1):
        node_case = replace(case, nodes=(args.node,))
        print(f"[{index}/{len(cases)}] {case.case_id} @ {args.node}", flush=True)
        summary = runner.run_case(node_case, concurrency=1)
        if summary:
            summary["node_id"] = args.node
            summary["cluster_nodes"] = len(case.nodes)
            write_json(node_dir / "cases" / f"{case.case_id}.json", summary)
            results.append(summary)
            print(
                f"  status={summary.get('status')} t_total={summary.get('t_max_ms')} ms",
                flush=True,
            )
            if summary.get("status") != "ok" and args.fail_fast:
                break
    write_json(
        node_dir / "node.json",
        {
            "node": args.node,
            "deployment": args.deployment,
            "platform": config.deployment(args.deployment).get("platform"),
            "devices": int(config.deployment(args.deployment).get("devices", 1)),
            "cluster_nodes": len(cases[0].nodes),
            "cases": [
                {"case_id": item.get("case_id"), "status": item.get("status")}
                for item in results
            ],
        },
    )
    print(f"node results: {node_dir}")
    return 0 if all(item.get("status") == "ok" for item in results) else 2


def cmd_aggregate(args: argparse.Namespace) -> int:
    config = Config.load(args.config)
    if args.run_dir is None and args.run_id is None:
        print("aggregate requires --run-id or --run-dir", file=sys.stderr)
        return 1
    base_dir = args.run_dir or config.results_dir() / args.run_id
    if not base_dir.exists():
        print(f"run directory not found: {base_dir}", file=sys.stderr)
        return 1
    from .aggregate import aggregate_nodes

    result = aggregate_nodes(base_dir)
    print(f"report: {result['report']} ({result['cases']} cases)")
    return 0


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
    preflight_parser.set_defaults(func=cmd_preflight)

    inputs_parser = sub.add_parser("inputs", help="generate/copy circuit inputs")
    _add_config(inputs_parser)
    _add_selection(inputs_parser)
    inputs_parser.add_argument("--run-dir", type=Path, default=None)
    inputs_parser.set_defaults(func=cmd_inputs)

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

    run_node_parser = sub.add_parser(
        "run-node", help="run this node's P3 share (one Slurm job per node)"
    )
    _add_config(run_node_parser)
    _add_selection(run_node_parser)
    run_node_parser.add_argument("--run-id", default=None)
    run_node_parser.add_argument("--run-dir", type=Path, default=None)
    run_node_parser.add_argument("--node", required=True)
    run_node_parser.add_argument("--force", action="store_true")
    run_node_parser.add_argument("--skip-cpu-reference", action="store_true")
    run_node_parser.add_argument("--fail-fast", action="store_true")
    run_node_parser.set_defaults(func=cmd_run_node, suite="p3")

    aggregate_parser = sub.add_parser(
        "aggregate", help="combine per-node P3 results into the final report"
    )
    _add_config(aggregate_parser)
    aggregate_parser.add_argument("--run-id", default=None)
    aggregate_parser.add_argument("--run-dir", type=Path, default=None)
    aggregate_parser.set_defaults(func=cmd_aggregate)

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
