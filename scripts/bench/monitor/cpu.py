"""CPU utilization sampler based on /proc/stat."""

from __future__ import annotations

import time

from .base import Sample


def _read_cpu() -> tuple[float, float] | None:
    try:
        with open("/proc/stat", "r", encoding="utf-8") as handle:
            fields = handle.readline().split()
    except OSError:
        return None
    if not fields or fields[0] != "cpu":
        return None
    values = [float(value) for value in fields[1:]]
    idle = values[3] + (values[4] if len(values) > 4 else 0.0)
    total = sum(values)
    return idle, total


def sample(host: str = "local", device: str = "cpu") -> list[Sample]:
    first = _read_cpu()
    if first is None:
        return []
    time.sleep(0.05)
    second = _read_cpu()
    if second is None:
        return []
    idle_delta = second[0] - first[0]
    total_delta = second[1] - first[1]
    util = None if total_delta <= 0 else max(0.0, min(100.0, 100.0 * (1.0 - idle_delta / total_delta)))
    return [Sample(host=host, device=device, timestamp=time.time(), util=util)]
