"""Resource monitor selection."""

from __future__ import annotations

from pathlib import Path

from . import amd, cpu, nvidia
from .base import Sample, Sampler, summarize_devices, summarize_utilization, write_samples_csv


class MonitorSet:
    def __init__(
        self,
        platform: str,
        *,
        host: str = "local",
        device_ids: list[str] | None = None,
        interval_ms: int = 100,
    ):
        self.platform = platform
        self.host = host
        self.device_ids = device_ids
        self.interval_s = max(0.01, interval_ms / 1000.0)
        self.samplers: list[Sampler] = []
        self.errors: list[str] = []
        self._build()

    def _build(self) -> None:
        if self.platform == "cpu":
            self.samplers.append(
                Sampler(self.host, self.interval_s, lambda: cpu.sample(self.host))
            )
        elif self.platform == "cuda":
            if nvidia.available():
                self.samplers.append(
                    Sampler(
                        self.host,
                        self.interval_s,
                        lambda: nvidia.sample(self.device_ids, self.host),
                    )
                )
            else:
                self.errors.append("nvidia-smi is not available")
        elif self.platform == "hip":
            if amd.available():
                self.samplers.append(
                    Sampler(
                        self.host,
                        self.interval_s,
                        lambda: amd.sample(self.device_ids, self.host),
                    )
                )
            else:
                self.errors.append("rocm-smi/amd-smi is not available")
        else:
            self.errors.append(f"unknown monitor platform: {self.platform}")

    def start(self) -> None:
        for sampler in self.samplers:
            sampler.start()

    def stop(self) -> list[Sample]:
        samples: list[Sample] = []
        for sampler in self.samplers:
            samples.extend(sampler.stop())
            if sampler.error:
                self.errors.append(sampler.error)
        return samples

    def write(self, path: Path, samples: list[Sample]) -> None:
        write_samples_csv(path, samples)

    def summarize(self, samples: list[Sample], *, trim_ratio: float, target: float) -> dict:
        return summarize_devices(samples, trim_ratio=trim_ratio, target=target)


__all__ = [
    "MonitorSet",
    "Sample",
    "summarize_devices",
    "summarize_utilization",
    "write_samples_csv",
]
