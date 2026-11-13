"""Local process execution with per-process log files."""

from __future__ import annotations

import os
import shlex
import subprocess
import time
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable

from .util import ensure_dir


@dataclass
class ProcessResult:
    argv: list[str]
    start_epoch: float
    end_epoch: float
    returncode: int
    stdout_path: Path
    stderr_path: Path
    timed_out: bool = False
    error: str | None = None

    @property
    def duration_s(self) -> float:
        return max(0.0, self.end_epoch - self.start_epoch)

    @property
    def stdout(self) -> str:
        return self.stdout_path.read_text(encoding="utf-8", errors="replace")

    @property
    def stderr(self) -> str:
        return self.stderr_path.read_text(encoding="utf-8", errors="replace")

    @property
    def ok(self) -> bool:
        return self.returncode == 0 and not self.timed_out and not self.error


def command_text(argv: Iterable[str]) -> str:
    return shlex.join(str(item) for item in argv)


def run_process(
    argv: list[str],
    *,
    log_dir: Path,
    name: str,
    env: dict[str, str] | None = None,
    cwd: Path | None = None,
    timeout_s: float | None = None,
    start_at: float | None = None,
) -> ProcessResult:
    ensure_dir(log_dir)
    stdout_path = log_dir / f"{name}.stdout.log"
    stderr_path = log_dir / f"{name}.stderr.log"
    process_env = os.environ.copy()
    if env:
        process_env.update({str(key): str(value) for key, value in env.items()})

    if start_at is not None:
        delay = start_at - time.time()
        if delay > 0:
            time.sleep(delay)

    start_epoch = time.time()
    timed_out = False
    error: str | None = None
    with stdout_path.open("w", encoding="utf-8") as stdout_handle, stderr_path.open(
        "w", encoding="utf-8"
    ) as stderr_handle:
        try:
            process = subprocess.Popen(
                [str(item) for item in argv],
                stdout=stdout_handle,
                stderr=stderr_handle,
                cwd=str(cwd) if cwd else None,
                env=process_env,
                start_new_session=True,
            )
        except OSError as exc:
            end_epoch = time.time()
            stderr_handle.write(f"failed to start process: {exc}\n")
            return ProcessResult(
                argv=argv,
                start_epoch=start_epoch,
                end_epoch=end_epoch,
                returncode=127,
                stdout_path=stdout_path,
                stderr_path=stderr_path,
                error=str(exc),
            )
        try:
            process.wait(timeout=timeout_s)
        except subprocess.TimeoutExpired:
            timed_out = True
            _terminate(process)
            process.wait()
        end_epoch = time.time()
        returncode = process.returncode if process.returncode is not None else -1
    return ProcessResult(
        argv=argv,
        start_epoch=start_epoch,
        end_epoch=end_epoch,
        returncode=returncode,
        stdout_path=stdout_path,
        stderr_path=stderr_path,
        timed_out=timed_out,
        error=error,
    )


def _terminate(process: subprocess.Popen[bytes]) -> None:
    try:
        os.killpg(os.getpgid(process.pid), 15)
    except OSError:
        try:
            process.terminate()
        except OSError:
            pass
    try:
        process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        try:
            os.killpg(os.getpgid(process.pid), 9)
        except OSError:
            try:
                process.kill()
            except OSError:
                pass


def run_parallel(
    specs: list[dict],
    *,
    start_at: float | None = None,
    max_workers: int | None = None,
) -> list[ProcessResult]:
    if not specs:
        return []
    workers = max_workers or len(specs)
    with ThreadPoolExecutor(max_workers=workers) as pool:
        futures = [
            pool.submit(
                run_process,
                spec["argv"],
                log_dir=spec["log_dir"],
                name=spec["name"],
                env=spec.get("env"),
                cwd=spec.get("cwd"),
                timeout_s=spec.get("timeout_s"),
                start_at=start_at,
            )
            for spec in specs
        ]
        return [future.result() for future in futures]
