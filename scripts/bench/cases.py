"""Test-case expansion and the 2^k circuit parameter mapping."""

from __future__ import annotations

from dataclasses import dataclass, replace
from typing import Any

from .config import Config
from .util import slugify


CIRCUITS = ("matmul", "fibonacci", "poseidon", "sql")
DENSITIES = ("full", "hybrid", "sparse")
SUPPORTED_DENSITY = {
    "matmul": DENSITIES,
    "fibonacci": ("full",),
    "poseidon": DENSITIES,
    "sql": DENSITIES,
}


@dataclass(frozen=True)
class Case:
    suite: str
    protocol: str
    circuit: str
    density: str
    platform: str
    deployment: str
    nodes: tuple[str, ...]
    devices: int
    concurrency: int
    repeats: int
    warmup: int
    k: int | None = None
    param: int | None = None
    stark_profile: str | None = None
    waves: int = 0
    proof_bits: int | None = None
    equivalent_bits: int | None = None
    cpu_reference: bool = True

    @property
    def case_id(self) -> str:
        if self.suite == "functional":
            suffix = (
                f"-{self.stark_profile}"
                if self.stark_profile and self.stark_profile != "cpu"
                else ""
            )
            return slugify(
                f"F-{self.protocol}-{self.circuit}-{self.platform}{suffix}"
            )
        scale = f"k{self.k}" if self.k is not None else f"b{self.proof_bits}"
        return slugify(
            f"{self.suite.upper()}-{self.circuit}-{self.density}-{scale}-"
            f"{self.platform}-{self.deployment}"
        )

    @property
    def display_platform(self) -> str:
        if self.protocol == "stark" and self.stark_profile:
            return self.stark_profile
        return self.platform

    @property
    def is_stark(self) -> bool:
        return self.protocol == "stark"

    @property
    def is_cpu(self) -> bool:
        return self.platform == "cpu"


def next_power_of_two(value: int) -> int:
    if value <= 1:
        return 1
    return 1 << (value - 1).bit_length()


def matmul_size_for_bits(k: int) -> int:
    """Largest s with next_power_of_two(s^3 + s^2) == 2^k."""
    target = 1 << k
    guess = int(round(target ** (1.0 / 3.0)))
    matches = [
        size
        for size in range(max(2, guess - 8), guess + 9)
        if next_power_of_two(size ** 3 + size ** 2) == target
    ]
    if not matches:
        raise ValueError(f"no matmul size fits exactly 2^{k}")
    return max(matches)


def param_for_bits(circuit: str, k: int) -> int:
    if k < 10:
        raise ValueError("k must be >= 10 for the SQL/Poseidon mappings")
    if circuit == "matmul":
        return matmul_size_for_bits(k)
    if circuit == "fibonacci":
        return 1 << k
    if circuit == "poseidon":
        return 1 << (k - 9)
    if circuit == "sql":
        return 1 << (k - 10)
    raise ValueError(f"unknown circuit: {circuit}")


def gen_args(circuit: str, param: int) -> list[str]:
    if circuit == "matmul":
        return ["matmul", str(param)]
    if circuit == "fibonacci":
        return ["fibonacci", str(param)]
    if circuit == "poseidon":
        return ["poseidon", str(param)]
    if circuit == "sql":
        return ["sql-aggregate", str(param)]
    raise ValueError(f"unknown circuit: {circuit}")


def stem_for(circuit: str, param: int, density: str) -> str:
    if circuit == "matmul":
        stem = f"matmul_{param}"
    elif circuit == "fibonacci":
        stem = f"fibonacci_{param}"
    elif circuit == "poseidon":
        stem = f"poseidon_{param}"
    elif circuit == "sql":
        stem = f"sql_aggregate_{param}"
    else:
        raise ValueError(f"unknown circuit: {circuit}")
    if density != "full":
        stem += f"_{density}"
    return stem


def validate_density(circuit: str, density: str) -> None:
    if density not in SUPPORTED_DENSITY.get(circuit, ()):
        raise ValueError(f"{circuit} does not support density {density}")


def _deployment_for_platform(config: Config, platform: str) -> str:
    return {"cpu": "cpu", "cuda": "cuda_1", "hip": "hip_1"}[platform]


def _case_from_config(
    config: Config,
    *,
    suite: str,
    protocol: str,
    circuit: str,
    density: str,
    platform: str,
    deployment: str,
    k: int | None,
    param: int | None,
    repeats: int,
    warmup: int,
    stark_profile: str | None = None,
    waves: int = 0,
    proof_bits: int | None = None,
    equivalent_bits: int | None = None,
) -> Case:
    deployment_cfg = config.deployment(deployment)
    nodes = tuple(str(node) for node in deployment_cfg.get("nodes", ["local"]))
    devices = int(deployment_cfg.get("devices", 1))
    concurrency_value = deployment_cfg.get("concurrency", 1)
    if isinstance(concurrency_value, str) and concurrency_value == "auto":
        concurrency = 1
    else:
        concurrency = int(concurrency_value)
    if platform == "cpu":
        concurrency = 1
    return Case(
        suite=suite,
        protocol=protocol,
        circuit=circuit,
        density=density,
        platform=platform,
        deployment=deployment,
        nodes=nodes,
        devices=devices,
        concurrency=concurrency,
        repeats=repeats,
        warmup=warmup,
        k=k,
        param=param,
        stark_profile=stark_profile,
        waves=waves,
        proof_bits=proof_bits,
        equivalent_bits=equivalent_bits,
        cpu_reference=platform != "cpu" and suite != "functional",
    )


def expand_functional(config: Config) -> list[Case]:
    section = config.get("functional", default={})
    protocols = section.get("protocols", ["groth16", "plonk", "stark"])
    circuits = section.get("circuits", list(CIRCUITS))
    platforms = section.get("platforms", ["cpu", "cuda", "hip"])
    sizes = section.get("sizes", {})
    repeats = int(section.get("repeats", config.get("run", "repeats", default=3)))
    warmup = int(config.get("run", "warmup", default=1))
    profiles = section.get(
        "stark_profiles",
        {"cpu_threads": 1, "dcu_sim_threads": 3, "gpu_sim_threads": 4},
    )

    cases: list[Case] = []
    for protocol in protocols:
        for circuit in circuits:
            param = int(sizes.get(circuit, 0))
            if param <= 0:
                raise ValueError(f"functional size for {circuit} is missing")
            if protocol == "stark":
                for label, key in (
                    ("cpu", "cpu_threads"),
                    ("GPU-sim", "gpu_sim_threads"),
                    ("DCU-sim", "dcu_sim_threads"),
                ):
                    _ = int(profiles.get(key, 0))
                    cases.append(
                        _case_from_config(
                            config,
                            suite="functional",
                            protocol=protocol,
                            circuit=circuit,
                            density="full",
                            platform="cpu",
                            deployment="cpu",
                            k=None,
                            param=param,
                            repeats=repeats,
                            warmup=warmup,
                            stark_profile=label,
                        )
                    )
            else:
                for platform in platforms:
                    cases.append(
                        _case_from_config(
                            config,
                            suite="functional",
                            protocol=protocol,
                            circuit=circuit,
                            density="full",
                            platform=platform,
                            deployment=_deployment_for_platform(config, platform),
                            k=None,
                            param=param,
                            repeats=repeats,
                            warmup=warmup,
                        )
                    )
    return cases


def _performance_axes(config: Config) -> tuple[list[str], list[str], list[str], list[int], int, int]:
    section = config.get("performance", default={})
    circuits = section.get("circuits", list(CIRCUITS))
    densities = section.get("densities", list(DENSITIES))
    platforms = ["cpu", "cuda", "hip"]
    k_bits = [int(value) for value in section.get("k_bits", list(range(20, 27)))]
    repeats = int(section.get("repeats", config.get("run", "repeats", default=3)))
    warmup = int(config.get("run", "warmup", default=1))
    return circuits, densities, platforms, k_bits, repeats, warmup


def expand_p1(config: Config) -> list[Case]:
    circuits, densities, platforms, k_bits, repeats, warmup = _performance_axes(config)
    cases: list[Case] = []
    for circuit in circuits:
        for density in densities:
            try:
                validate_density(circuit, density)
            except ValueError:
                continue
            for k in k_bits:
                for platform in platforms:
                    cases.append(
                        _case_from_config(
                            config,
                            suite="p1",
                            protocol="groth16",
                            circuit=circuit,
                            density=density,
                            platform=platform,
                            deployment=_deployment_for_platform(config, platform),
                            k=k,
                            param=param_for_bits(circuit, k),
                            repeats=repeats,
                            warmup=warmup,
                        )
                    )
    return cases


def expand_p2(config: Config) -> list[Case]:
    circuits, densities, _, k_bits, repeats, warmup = _performance_axes(config)
    section = config.get("performance", "p2", default={})
    devices = section.get("devices", {"cuda": 4, "hip": 8})
    cases: list[Case] = []
    for circuit in circuits:
        for density in densities:
            try:
                validate_density(circuit, density)
            except ValueError:
                continue
            for k in k_bits:
                for platform in ("cuda", "hip"):
                    deployment = f"{platform}_{int(devices.get(platform, 4 if platform == 'cuda' else 8))}"
                    if deployment not in config.get("deployments", default={}):
                        deployment = "cuda_4" if platform == "cuda" else "hip_8"
                    cases.append(
                        _case_from_config(
                            config,
                            suite="p2",
                            protocol="groth16",
                            circuit=circuit,
                            density=density,
                            platform=platform,
                            deployment=deployment,
                            k=k,
                            param=param_for_bits(circuit, k),
                            repeats=repeats,
                            warmup=warmup,
                        )
                    )
    return cases


def expand_p3(config: Config) -> list[Case]:
    circuits, densities, _, _, repeats, warmup = _performance_axes(config)
    section = config.get("performance", "p3", default={})
    proof_bits = int(section.get("proof_bits", 26))
    equivalent_bits = int(section.get("equivalent_bits", 30))
    nodes = section.get("nodes", {"cuda": 3, "hip": 2})
    devices = section.get("devices_per_node", {"cuda": 4, "hip": 8})
    waves = int(section.get("waves_per_card", 16))
    cases: list[Case] = []
    for circuit in circuits:
        for density in densities:
            try:
                validate_density(circuit, density)
            except ValueError:
                continue
            for platform in ("cuda", "hip"):
                deployment = (
                    f"{platform}_{int(nodes.get(platform, 1))}x"
                    f"{int(devices.get(platform, 1))}"
                )
                if deployment not in config.get("deployments", default={}):
                    deployment = "cuda_3x4" if platform == "cuda" else "hip_2x8"
                cases.append(
                    _case_from_config(
                        config,
                        suite="p3",
                        protocol="groth16",
                        circuit=circuit,
                        density=density,
                        platform=platform,
                        deployment=deployment,
                        k=proof_bits,
                        param=param_for_bits(circuit, proof_bits),
                        repeats=repeats,
                        warmup=0,
                        waves=waves,
                        proof_bits=proof_bits,
                        equivalent_bits=equivalent_bits,
                    )
                )
    return cases


def expand_suite(config: Config, suite: str) -> list[Case]:
    if suite == "functional":
        return expand_functional(config)
    if suite == "p1":
        return expand_p1(config)
    if suite == "p2":
        return expand_p2(config)
    if suite == "p3":
        return expand_p3(config)
    if suite == "all":
        return (
            expand_functional(config)
            + expand_p1(config)
            + expand_p2(config)
            + expand_p3(config)
        )
    raise ValueError(f"unknown suite: {suite}")


def filter_tier(cases: list[Case], tier: str, config: Config) -> list[Case]:
    if tier == "full":
        return cases
    if tier == "smoke":
        selected = [
            case
            for case in cases
            if case.suite == "functional"
            or (
                case.circuit == "matmul"
                and case.density == "full"
                and (case.suite == "p3" or case.k in (20, 22))
            )
        ]
        return [
            replace(case, repeats=1, waves=1) if case.suite == "p3" else case
            for case in selected
        ]
    if tier == "standard":
        return [
            case
            for case in cases
            if case.suite == "functional"
            or case.suite == "p3"
            or case.k in (20, 22, 24)
        ]
    raise ValueError(f"unknown tier: {tier}")


def sort_cases(cases: list[Case]) -> list[Case]:
    order = {"functional": 0, "p1": 1, "p2": 2, "p3": 3}
    return sorted(
        cases,
        key=lambda case: (
            order.get(case.suite, 9),
            case.protocol,
            case.circuit,
            case.density,
            case.k or 0,
            case.platform,
            case.deployment,
        ),
    )
