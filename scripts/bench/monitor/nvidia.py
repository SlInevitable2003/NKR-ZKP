"""NVIDIA GPU sampler using nvidia-smi."""

from __future__ import annotations

import csv
import io
import subprocess
import time

from .base import Sample


QUERY_FIELDS = (
    "index",
    "uuid",
    "utilization.gpu",
    "utilization.memory",
    "memory.used",
    "memory.total",
    "power.draw",
    "temperature.gpu",
    "clocks.sm",
)


def _number(value: str) -> float | None:
    value = value.strip()
    if not value or value.upper() in ("N/A", "[N/A]", "NOT SUPPORTED"):
        return None
    try:
        return float(value)
    except ValueError:
        return None


def available() -> bool:
    try:
        result = subprocess.run(
            ["nvidia-smi", "-L"],
            capture_output=True,
            text=True,
            timeout=10,
            check=False,
        )
        return result.returncode == 0
    except (OSError, subprocess.SubprocessError):
        return False


def list_devices() -> list[str]:
    try:
        result = subprocess.run(
            ["nvidia-smi", "--query-gpu=uuid", "--format=csv,noheader"],
            capture_output=True,
            text=True,
            timeout=10,
            check=False,
        )
    except (OSError, subprocess.SubprocessError):
        return []
    if result.returncode != 0:
        return []
    return [line.strip() for line in result.stdout.splitlines() if line.strip()]


def sample(device_uuids: list[str] | None = None, host: str = "local") -> list[Sample]:
    command = [
        "nvidia-smi",
        f"--query-gpu={','.join(QUERY_FIELDS)}",
        "--format=csv,noheader,nounits",
    ]
    try:
        result = subprocess.run(
            command, capture_output=True, text=True, timeout=10, check=False
        )
    except (OSError, subprocess.SubprocessError):
        return []
    if result.returncode != 0:
        return []
    wanted = set(device_uuids or [])
    samples: list[Sample] = []
    for row in csv.reader(io.StringIO(result.stdout)):
        if len(row) < len(QUERY_FIELDS):
            continue
        index, uuid, util, util_mem, used, total, power, temp, clock = row[
            : len(QUERY_FIELDS)
        ]
        device = uuid.strip() or f"gpu{index.strip()}"
        if wanted and device not in wanted and index.strip() not in wanted:
            continue
        samples.append(
            Sample(
                host=host,
                device=device,
                timestamp=time.time(),
                util=_number(util),
                mem_used_mb=_number(used),
                mem_total_mb=_number(total),
                power_w=_number(power),
                temp_c=_number(temp),
                clock_mhz=_number(clock),
                raw={"index": index.strip(), "util_memory": _number(util_mem)},
            )
        )
    return samples
