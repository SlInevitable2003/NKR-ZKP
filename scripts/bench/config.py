"""Configuration loading and validation for the benchmark harness."""

from __future__ import annotations

from copy import deepcopy
from pathlib import Path
from typing import Any

from .util import REPO_ROOT, deep_merge, read_json


DEFAULT_CONFIG: dict[str, Any] = {
    "run": {
        "results_dir": "results",
        "repeats": 3,
        "warmup": 1,
        "timeout_s": 7200,
        "verify_proofs": "all",
        "fail_fast": False,
    },
    "functional": {
        "protocols": ["groth16", "plonk", "stark"],
        "circuits": ["matmul", "fibonacci", "poseidon", "sql"],
        "platforms": ["cpu", "cuda", "hip"],
        "density": "full",
        "sizes": {
            "matmul": 16,
            "fibonacci": 1024,
            "poseidon": 8,
            "sql": 16,
        },
        "repeats": 3,
        "stark_profiles": {
            "cpu_threads": 1,
            "dcu_sim_threads": 3,
            "gpu_sim_threads": 4,
        },
    },
    "performance": {
        "protocol": "groth16",
        "circuits": ["matmul", "fibonacci", "poseidon", "sql"],
        "densities": ["full", "hybrid", "sparse"],
        "k_bits": [20, 21, 22, 23, 24, 25, 26],
        "repeats": 3,
        "cpu_reference": "single_machine_single_thread",
        "p1": {"concurrency": "auto", "max_concurrency": 16},
        "p2": {
            "devices": {"cuda": 4, "hip": 8},
            "concurrency": "auto",
            "max_concurrency": 8,
        },
        "p3": {
            "proof_bits": 26,
            "equivalent_bits": 30,
            "nodes": {"cuda": 3, "hip": 2},
            "devices_per_node": {"cuda": 4, "hip": 8},
            "waves_per_card": 16,
            "concurrency": 1,
        },
    },
    "setup": {
        "reuse": False,
        "per_case": True,
        "per_node": True,
    },
    "utilization": {
        "target": 0.90,
        "sample_ms": 100,
        "trim_ratio": 0.10,
        "qualification": "gate",
    },
    "platforms": {
        "cpu": {
            "kind": "cpu",
            "groth16_bin": "build/groth16/bin",
            "groth16_data": "build/groth16/data",
            "plonk_bin": "build/plonk/bin",
            "plonk_data": "build/plonk/data",
            "stark_bin": "protocols/stark/winterfell/target/release/winterfell",
        },
        "cuda": {
            "kind": "cuda",
            "groth16_bin": "build/groth16/bin",
            "groth16_data": "build/groth16/data",
            "plonk_bin": "build/plonk/bin",
            "plonk_data": "build/plonk/data",
            "stark_bin": "protocols/stark/winterfell/target/release/winterfell",
        },
        "hip": {
            "kind": "hip",
            "groth16_bin": "build/groth16/bin",
            "groth16_data": "build/groth16/data",
            "plonk_bin": "build/plonk/bin",
            "plonk_data": "build/plonk/data",
            "stark_bin": "protocols/stark/winterfell/target/release/winterfell",
        },
    },
    "deployments": {
        "cpu": {"platform": "cpu", "nodes": ["local"], "devices": 1, "concurrency": 1},
        "cuda_1": {"platform": "cuda", "nodes": ["local"], "devices": 1, "concurrency": "auto"},
        "hip_1": {"platform": "hip", "nodes": ["local"], "devices": 1, "concurrency": "auto"},
        "cuda_4": {"platform": "cuda", "nodes": ["gpu01"], "devices": 4, "concurrency": "auto"},
        "hip_8": {"platform": "hip", "nodes": ["dcu01"], "devices": 8, "concurrency": "auto"},
        "cuda_3x4": {
            "platform": "cuda",
            "nodes": ["gpu01", "gpu02", "gpu03"],
            "devices": 4,
            "concurrency": 1,
        },
        "hip_2x8": {
            "platform": "hip",
            "nodes": ["dcu01", "dcu02"],
            "devices": 8,
            "concurrency": 1,
        },
    },
}


class Config:
    def __init__(self, data: dict[str, Any], path: Path | None = None):
        self.data = data
        self.path = path
        self.repo_root = REPO_ROOT
        self.machines = self._load_machines()

    @classmethod
    def load(cls, path: Path | str | None = None) -> "Config":
        config_path = Path(path) if path else REPO_ROOT / "scripts/configs/matrix.json"
        override = read_json(config_path, default={}) if config_path.exists() else {}
        if not isinstance(override, dict):
            raise ValueError(f"config must be a JSON object: {config_path}")
        data = deep_merge(deepcopy(DEFAULT_CONFIG), override)
        return cls(data, config_path if config_path.exists() else None)

    def _load_machines(self) -> dict[str, Any]:
        configured = self.data.get("machines_file")
        if not configured:
            return {}
        path = Path(configured)
        if not path.is_absolute():
            path = self.repo_root / path
        machines = read_json(path, default={})
        if not isinstance(machines, dict):
            machines = {}
        return machines

    def get(self, *keys: str, default: Any = None) -> Any:
        value: Any = self.data
        for key in keys:
            if not isinstance(value, dict) or key not in value:
                return default
            value = value[key]
        return value

    def resolve(self, path: str | Path) -> Path:
        path = Path(path)
        return path if path.is_absolute() else self.repo_root / path

    def platform(self, name: str) -> dict[str, Any]:
        platforms = self.data.get("platforms", {})
        if name not in platforms:
            raise KeyError(f"unknown platform: {name}")
        return platforms[name]

    def deployment(self, name: str) -> dict[str, Any]:
        deployments = self.data.get("deployments", {})
        if name not in deployments:
            raise KeyError(f"unknown deployment: {name}")
        return deployments[name]

    def node(self, name: str) -> dict[str, Any]:
        if name == "local":
            return {"host": "local", "local": True}
        node = self.machines.get(name)
        if node is None:
            return {"host": name, "local": False}
        return node

    def node_is_local(self, name: str) -> bool:
        return bool(self.node(name).get("local", name == "local"))

    def bin_for(self, protocol: str, platform: str) -> Path:
        key = f"{protocol}_bin"
        value = self.platform(platform).get(key)
        if not value:
            raise KeyError(f"platform {platform} has no {key}")
        return self.resolve(value)

    def data_dir_for(self, protocol: str, platform: str) -> Path:
        key = f"{protocol}_data"
        value = self.platform(platform).get(key)
        if not value:
            raise KeyError(f"platform {platform} has no {key}")
        return self.resolve(value)

    def results_dir(self) -> Path:
        return self.resolve(self.get("run", "results_dir", default="results"))

    def concurrency(self, deployment_name: str) -> int | str:
        value = self.deployment(deployment_name).get("concurrency", 1)
        if isinstance(value, str) and value == "auto":
            return "auto"
        return int(value)

    def validate(self) -> None:
        for name, deployment in self.data.get("deployments", {}).items():
            platform = deployment.get("platform")
            if platform not in self.data.get("platforms", {}):
                raise ValueError(f"deployment {name} references unknown platform {platform}")
            if not deployment.get("nodes"):
                raise ValueError(f"deployment {name} has no nodes")
        for protocol in ("groth16", "plonk"):
            for platform in self.data.get("platforms", {}):
                self.bin_for(protocol, platform)  # raises on missing key
