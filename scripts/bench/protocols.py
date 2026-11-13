"""Protocol command construction and output parsing."""

from __future__ import annotations

import re
from pathlib import Path
from typing import Any

from .cases import Case, gen_args
from .config import Config


PROFILE_RE = re.compile(r"^\[profile\]\s+(\S+)\s*=\s*([0-9]+(?:\.[0-9]+)?)\s*ms\s*$")
STARK_PROVE_RE = re.compile(r"Proof generated in\s+([0-9]+)\s+ms")
STARK_VERIFY_RE = re.compile(r"Proof verified in\s+([0-9]+(?:\.[0-9]+)?)\s+ms")
STARK_SIZE_RE = re.compile(r"Proof size:\s+([0-9]+(?:\.[0-9]+)?)\s+KB")


def backend_for_platform(platform: str) -> str:
    if platform not in ("cpu", "cuda", "hip"):
        raise ValueError(f"unsupported platform: {platform}")
    return platform


def protocol_bin(config: Config, protocol: str, platform: str) -> Path:
    return config.bin_for(protocol, platform)


def executable(config: Config, protocol: str, platform: str, stage: str) -> str:
    if protocol == "stark":
        return str(config.bin_for("stark", platform))
    return str(config.bin_for(protocol, platform) / f"{protocol}-{stage}")


def build_gen(config: Config, protocol: str, platform: str, case: Case) -> list[str]:
    if protocol not in ("groth16", "plonk"):
        raise ValueError(f"gen is not used for {protocol}")
    if case.param is None:
        raise ValueError("case has no circuit parameter")
    argv = [executable(config, protocol, platform, "gen")]
    if case.circuit != "fibonacci":
        argv.extend(["--density", case.density])
    argv.extend(gen_args(case.circuit, case.param))
    return argv


def build_setup(
    config: Config, protocol: str, platform: str, case: Case, workdir: Path, stem: str
) -> list[str]:
    if protocol not in ("groth16", "plonk"):
        raise ValueError(f"setup is not used for {protocol}")
    return [
        executable(config, protocol, platform, "setup"),
        "--profile",
        "--backend",
        backend_for_platform(platform),
        "--path",
        str(workdir),
        f"{stem}.bin",
    ]


def build_prove(
    config: Config,
    protocol: str,
    platform: str,
    case: Case,
    workdir: Path,
    stem: str,
    proof_path: Path,
) -> list[str]:
    if protocol not in ("groth16", "plonk"):
        raise ValueError(f"prove is not used for {protocol}")
    return [
        executable(config, protocol, platform, "prove"),
        "--profile",
        "--backend",
        backend_for_platform(platform),
        "--path",
        str(workdir),
        "--output",
        str(proof_path),
        f"{stem}.bin",
    ]


def build_verify(
    config: Config,
    protocol: str,
    platform: str,
    case: Case,
    workdir: Path,
    stem: str,
    proof_path: Path,
) -> list[str]:
    if protocol not in ("groth16", "plonk"):
        raise ValueError(f"verify is not used for {protocol}")
    return [
        executable(config, protocol, platform, "verify"),
        "--path",
        str(workdir),
        "--proof",
        str(proof_path),
        f"{stem}.bin",
    ]


def stark_command(config: Config, case: Case, threads: int) -> list[str]:
    if case.param is None:
        raise ValueError("STARK case has no parameter")
    binary = str(config.bin_for("stark", "cpu"))
    subcommand = {
        "matmul": "matrix-mul",
        "fibonacci": "fib",
        "poseidon": "poseidon",
        "sql": "sql",
    }[case.circuit]
    argv = [binary, subcommand, "-n", str(case.param)]
    if case.circuit != "fibonacci":
        argv.extend(["--density", case.density])
    argv.append("--profile")
    _ = threads
    return argv


def stark_threads(config: Config, case: Case) -> int:
    profiles = config.get(
        "functional",
        "stark_profiles",
        default={"cpu_threads": 1, "dcu_sim_threads": 3, "gpu_sim_threads": 4},
    )
    if case.stark_profile == "DCU-sim":
        return int(profiles.get("dcu_sim_threads", 3))
    if case.stark_profile == "GPU-sim":
        return int(profiles.get("gpu_sim_threads", 4))
    return int(profiles.get("cpu_threads", 1))


def parse_profile(text: str) -> dict[str, float]:
    values: dict[str, float] = {}
    for line in text.splitlines():
        match = PROFILE_RE.match(line.strip())
        if match:
            values[match.group(1)] = float(match.group(2))
    return values


def parse_stark(text: str) -> dict[str, Any]:
    result: dict[str, Any] = {}
    prove = STARK_PROVE_RE.search(text)
    verify = STARK_VERIFY_RE.search(text)
    size = STARK_SIZE_RE.search(text)
    if prove:
        result["prove_total_ms"] = float(prove.group(1))
    if verify:
        result["verify_total_ms"] = float(verify.group(1))
    if size:
        result["proof_size_kb"] = float(size.group(1))
    return result


def profile_label(protocol: str) -> str:
    if protocol == "groth16":
        return "groth16.prove.total"
    if protocol == "plonk":
        return "plonk.prove.total"
    raise ValueError(f"no profile label for {protocol}")


def program_total_label(protocol: str) -> str:
    return f"{protocol}.prove.program_total"


def write_proof_label(protocol: str) -> str:
    return f"{protocol}.prove.write_proof"


def expected_backend(platform: str) -> str:
    return backend_for_platform(platform)


def backend_line(protocol: str) -> str:
    if protocol == "groth16":
        return "Groth16 prove backend:"
    if protocol == "plonk":
        return "PLONK prove backend:"
    raise ValueError(f"no backend line for {protocol}")


def verify_ok_text(protocol: str) -> str:
    _ = protocol
    return "Proof verification succeeded."
