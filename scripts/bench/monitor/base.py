"""Resource sampler base types."""

from __future__ import annotations

import statistics
import threading
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Callable

from ..util import ensure_dir


@dataclass
class Sample:
    host: str
    device: str
    timestamp: float
    util: float | None = None
    mem_used_mb: float | None = None
    mem_total_mb: float | None = None
    power_w: float | None = None
    temp_c: float | None = None
    clock_mhz: float | None = None
    raw: dict = field(default_factory=dict)


class Sampler(threading.Thread):
    def __init__(
        self,
        host: str,
        interval_s: float,
        sample_fn: Callable[[], list[Sample]],
    ):
        super().__init__(daemon=True)
        self.host = host
        self.interval_s = interval_s
        self.sample_fn = sample_fn
        self.samples: list[Sample] = []
        self.error: str | None = None
        self._stop = threading.Event()

    def run(self) -> None:
        while not self._stop.is_set():
            started = time.time()
            try:
                self.samples.extend(self.sample_fn())
            except Exception as exc:  # keep the benchmark alive
                self.error = str(exc)
            elapsed = time.time() - started
            self._stop.wait(max(0.0, self.interval_s - elapsed))

    def stop(self) -> list[Sample]:
        self._stop.set()
        self.join(timeout=5)
        return self.samples


def _percentile(values: list[float], fraction: float) -> float | None:
    if not values:
        return None
    ordered = sorted(values)
    if len(ordered) == 1:
        return ordered[0]
    position = (len(ordered) - 1) * fraction
    lower = int(position)
    upper = min(lower + 1, len(ordered) - 1)
    weight = position - lower
    return ordered[lower] * (1.0 - weight) + ordered[upper] * weight


def summarize_utilization(
    samples: list[Sample],
    *,
    trim_ratio: float = 0.10,
    target: float = 0.90,
) -> dict:
    values = [sample.util for sample in samples if sample.util is not None]
    if not values:
        return {
            "util_mean": None,
            "util_p10": None,
            "util_min": None,
            "util_max": None,
            "util_ge_target_ratio": None,
            "sample_count": 0,
        }
    values.sort()
    if trim_ratio > 0 and len(values) >= 5:
        cut = int(len(values) * trim_ratio)
        if cut > 0 and len(values) - 2 * cut >= 2:
            values = values[cut : len(values) - cut]
    target_pct = target * 100.0
    return {
        "util_mean": statistics.fmean(values),
        "util_p10": _percentile(values, 0.10),
        "util_min": min(values),
        "util_max": max(values),
        "util_ge_target_ratio": sum(1 for value in values if value >= target_pct)
        / len(values),
        "sample_count": len(values),
    }


def summarize_devices(
    samples: list[Sample], *, trim_ratio: float, target: float
) -> dict[str, dict]:
    devices: dict[str, list[Sample]] = {}
    for sample in samples:
        devices.setdefault(sample.device, []).append(sample)
    return {
        device: summarize_utilization(
            device_samples, trim_ratio=trim_ratio, target=target
        )
        for device, device_samples in devices.items()
    }


def write_samples_csv(path: Path, samples: list[Sample]) -> None:
    import csv

    ensure_dir(path.parent)
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.writer(handle)
        writer.writerow(
            [
                "timestamp",
                "host",
                "device",
                "util",
                "mem_used_mb",
                "mem_total_mb",
                "power_w",
                "temp_c",
                "clock_mhz",
            ]
        )
        for sample in samples:
            writer.writerow(
                [
                    f"{sample.timestamp:.6f}",
                    sample.host,
                    sample.device,
                    "" if sample.util is None else f"{sample.util:.3f}",
                    "" if sample.mem_used_mb is None else f"{sample.mem_used_mb:.1f}",
                    "" if sample.mem_total_mb is None else f"{sample.mem_total_mb:.1f}",
                    "" if sample.power_w is None else f"{sample.power_w:.2f}",
                    "" if sample.temp_c is None else f"{sample.temp_c:.1f}",
                    "" if sample.clock_mhz is None else f"{sample.clock_mhz:.1f}",
                ]
            )
