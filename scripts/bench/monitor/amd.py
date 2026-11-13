"""DCU/AMD sampler with rocm-smi or amd-smi fallbacks."""

from __future__ import annotations

import csv
import io
import re
import subprocess
import time

from .base import Sample


def available() -> bool:
    for command in (["rocm-smi", "--version"], ["amd-smi", "version"]):
        try:
            result = subprocess.run(
                command, capture_output=True, text=True, timeout=10, check=False
            )
            if result.returncode == 0:
                return True
        except (OSError, subprocess.SubprocessError):
            continue
    return False


def _number(text: str) -> float | None:
    match = re.search(r"-?[0-9]+(?:\.[0-9]+)?", text)
    return float(match.group(0)) if match else None


def _parse_rocm_csv(text: str, host: str) -> list[Sample]:
    samples: list[Sample] = []
    rows = list(csv.reader(io.StringIO(text)))
    if not rows:
        return samples
    header = [cell.strip().lower() for cell in rows[0]]
    use_idx = next(
        (index for index, cell in enumerate(header) if "use" in cell and "mem" not in cell),
        None,
    )
    mem_idx = next(
        (index for index, cell in enumerate(header) if "memory use" in cell or "vram" in cell),
        None,
    )
    power_idx = next(
        (index for index, cell in enumerate(header) if "power" in cell), None
    )
    temp_idx = next(
        (index for index, cell in enumerate(header) if "temp" in cell), None
    )
    clock_idx = next(
        (index for index, cell in enumerate(header) if "clock" in cell), None
    )
    for row in rows[1:]:
        if not row:
            continue
        device = row[0].strip() or f"dcu{len(samples)}"
        samples.append(
            Sample(
                host=host,
                device=device,
                timestamp=time.time(),
                util=_number(row[use_idx]) if use_idx is not None and use_idx < len(row) else None,
                mem_used_mb=_number(row[mem_idx]) if mem_idx is not None and mem_idx < len(row) else None,
                power_w=_number(row[power_idx]) if power_idx is not None and power_idx < len(row) else None,
                temp_c=_number(row[temp_idx]) if temp_idx is not None and temp_idx < len(row) else None,
                clock_mhz=_number(row[clock_idx]) if clock_idx is not None and clock_idx < len(row) else None,
            )
        )
    return samples


def _parse_text(text: str, host: str) -> list[Sample]:
    """Parse rocm-smi non-CSV blocks such as:
    GPU[0] : GPU use (%): 95
    """
    devices: dict[str, dict] = {}
    current: str | None = None
    for line in text.splitlines():
        device_match = re.search(r"(GPU\[[^\]]+\]|card[0-9]+)", line)
        if device_match:
            current = device_match.group(1)
            devices.setdefault(current, {})
        if current is None:
            continue
        lowered = line.lower()
        if "gpu use" in lowered or "gpu_busy_percent" in lowered:
            devices[current]["util"] = _number(line.split(":")[-1])
        elif "memory use" in lowered or "vram" in lowered:
            devices[current]["mem_used_mb"] = _number(line.split(":")[-1])
        elif "power" in lowered and "average" not in lowered:
            devices[current]["power_w"] = _number(line.split(":")[-1])
        elif "temperature" in lowered or "temp" in lowered:
            devices[current]["temp_c"] = _number(line.split(":")[-1])
        elif "sclk" in lowered or "clock" in lowered:
            devices[current]["clock_mhz"] = _number(line.split(":")[-1])
    return [
        Sample(host=host, device=device, timestamp=time.time(), **values)
        for device, values in devices.items()
    ]


def sample(device_ids: list[str] | None = None, host: str = "local") -> list[Sample]:
    commands = [
        ["rocm-smi", "--showuse", "--showmemuse", "--showtemp", "--showpower", "--csv"],
        ["rocm-smi", "--showuse", "--showmemuse", "--showtemp", "--showpower"],
        ["amd-smi", "metric", "--usage", "--csv"],
    ]
    for command in commands:
        try:
            result = subprocess.run(
                command, capture_output=True, text=True, timeout=15, check=False
            )
        except (OSError, subprocess.SubprocessError):
            continue
        if result.returncode != 0 or not result.stdout.strip():
            continue
        samples = (
            _parse_rocm_csv(result.stdout, host)
            if "--csv" in command
            else _parse_text(result.stdout, host)
        )
        if samples:
            wanted = set(device_ids or [])
            if wanted:
                samples = [
                    item
                    for item in samples
                    if item.device in wanted
                    or any(item.device.endswith(device) for device in wanted)
                ]
            return samples
    return []
