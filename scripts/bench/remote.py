"""SSH/rsync helpers for multi-machine P3 runs.

The driver writes a small JSON plan, copies it to the node, runs a worker
subcommand over SSH, and copies the worker's result back.  All protocol
binaries and artifacts are expected to exist on the node at the configured
repo path; setup is executed per node by the worker.
"""

from __future__ import annotations

import shlex
import subprocess
from dataclasses import dataclass
from pathlib import Path
from typing import Any

from .config import Config
from .executor import ProcessResult, run_process
from .util import ensure_dir, write_json


@dataclass
class RemoteNode:
    name: str
    host: str
    user: str
    port: int
    repo: Path
    workdir: Path
    python: str
    env: list[str]
    platform: str


def node_from_config(config: Config, name: str) -> RemoteNode:
    node = config.node(name)
    if node.get("local", False) or name == "local":
        raise ValueError(f"node {name} is local; remote helper is not needed")
    return RemoteNode(
        name=name,
        host=str(node.get("host", name)),
        user=str(node.get("user", "")),
        port=int(node.get("port", 22)),
        repo=Path(str(node.get("repo", config.repo_root))),
        workdir=Path(str(node.get("workdir", "/tmp/nkr-bench"))),
        python=str(node.get("python", "python3")),
        env=[str(item) for item in node.get("env", [])],
        platform=str(node.get("platform", "")),
    )


def ssh_argv(node: RemoteNode, command: str) -> list[str]:
    target = f"{node.user}@{node.host}" if node.user else node.host
    return [
        "ssh",
        "-o",
        "BatchMode=yes",
        "-o",
        "StrictHostKeyChecking=accept-new",
        "-p",
        str(node.port),
        target,
        command,
    ]


def rsync_argv(node: RemoteNode, source: str, target: str, *, delete: bool = False) -> list[str]:
    target_spec = f"{node.user}@{node.host}:{target}" if node.user else f"{node.host}:{target}"
    argv = ["rsync", "-az"]
    if delete:
        argv.append("--delete")
    argv.extend([source, target_spec])
    return argv


def _remote_prefix(node: RemoteNode) -> str:
    pieces = [f"cd {shlex.quote(str(node.repo))}"]
    pieces.extend(node.env)
    return " && ".join(pieces)


def run_worker(
    node: RemoteNode,
    *,
    plan_path: Path,
    log_dir: Path,
    name: str,
    timeout_s: float | None,
    start_at: float | None = None,
) -> ProcessResult:
    command = (
        f"{_remote_prefix(node)} && "
        f"{shlex.quote(node.python)} -m scripts.bench _worker "
        f"--plan {shlex.quote(str(plan_path))}"
    )
    return run_process(
        ssh_argv(node, f"bash -lc {shlex.quote(command)}"),
        log_dir=log_dir,
        name=name,
        timeout_s=timeout_s,
        start_at=start_at,
    )


def ensure_remote_dir(node: RemoteNode, path: Path, log_dir: Path, name: str) -> bool:
    command = f"mkdir -p {shlex.quote(str(path))}"
    result = run_process(
        ssh_argv(node, f"bash -lc {shlex.quote(command)}"),
        log_dir=log_dir,
        name=name,
    )
    return result.ok


def copy_to(
    node: RemoteNode, source: Path, target: Path, *, log_dir: Path, name: str
) -> bool:
    result = run_process(
        rsync_argv(node, str(source), str(target)),
        log_dir=log_dir,
        name=name,
    )
    return result.ok


def copy_from(
    node: RemoteNode, source: Path, target: Path, *, log_dir: Path, name: str
) -> bool:
    ensure_dir(target.parent)
    target_spec = f"{node.user}@{node.host}:{source}" if node.user else f"{node.host}:{source}"
    result = run_process(
        ["rsync", "-az", target_spec, str(target)],
        log_dir=log_dir,
        name=name,
    )
    return result.ok


def write_plan(path: Path, plan: dict[str, Any]) -> None:
    write_json(path, plan)


def check_node(node: RemoteNode, paths: list[Path], *, log_dir: Path) -> tuple[bool, str]:
    checks = (
        " && ".join(f"test -x {shlex.quote(str(path))}" for path in paths)
        if paths
        else "test -d ."
    )
    command = f"{_remote_prefix(node)} && {checks}"
    result = run_process(
        ssh_argv(node, f"bash -lc {shlex.quote(command)}"),
        log_dir=log_dir,
        name=f"{node.name}-preflight",
    )
    return result.ok, result.stderr
