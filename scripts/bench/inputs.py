"""Circuit generation and per-case setup."""

from __future__ import annotations

import os
import re
import shutil
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Any

from .cases import Case, stem_for
from .config import Config
from .executor import run_process
from .protocols import build_gen, build_setup
from .util import (
    ensure_dir,
    file_lock,
    read_json,
    sha256_file,
    write_json,
)


CONSTRAINTS_RE = re.compile(r"\*\s*R1CS constraints:\s*([0-9]+)")
VARIABLES_RE = re.compile(r"\*\s*R1CS variables:\s*([0-9]+)")


@dataclass
class InputArtifact:
    case: Case
    stem: str
    directory: Path
    r1cs: Path
    witness: Path
    constraints: int | None
    variables: int | None
    manifest: dict[str, Any]

    def as_dict(self) -> dict[str, Any]:
        return {
            "stem": self.stem,
            "directory": str(self.directory),
            "r1cs": str(self.r1cs),
            "witness": str(self.witness),
            "constraints": self.constraints,
            "variables": self.variables,
            "manifest": self.manifest,
        }


@dataclass
class SetupArtifact:
    case: Case
    stem: str
    directory: Path
    proving_key: Path
    verification_key: Path
    srs: Path | None
    duration_s: float
    manifest: dict[str, Any]

    def as_dict(self) -> dict[str, Any]:
        return {
            "stem": self.stem,
            "directory": str(self.directory),
            "proving_key": str(self.proving_key),
            "verification_key": str(self.verification_key),
            "srs": None if self.srs is None else str(self.srs),
            "duration_s": self.duration_s,
            "manifest": self.manifest,
        }


class InputManager:
    def __init__(self, config: Config):
        self.config = config
        self.cache_root = ensure_dir(config.resolve("artifacts/inputs"))

    def _cache_dir(self, case: Case) -> Path:
        if case.param is None:
            raise ValueError("case has no circuit parameter")
        return ensure_dir(
            self.cache_root
            / case.protocol
            / case.circuit
            / str(case.param)
            / case.density
        )

    def ensure(self, case: Case) -> InputArtifact | None:
        if case.is_stark:
            return None
        if case.param is None:
            raise ValueError("case has no circuit parameter")
        cache_dir = self._cache_dir(case)
        manifest_path = cache_dir / "manifest.json"
        r1cs_path = cache_dir / "circuit.bin"
        witness_path = cache_dir / "witness.bin"
        if manifest_path.exists() and r1cs_path.exists() and witness_path.exists():
            manifest = read_json(manifest_path, default={})
            return InputArtifact(
                case=case,
                stem=str(manifest.get("stem", stem_for(case.circuit, case.param, case.density))),
                directory=cache_dir,
                r1cs=r1cs_path,
                witness=witness_path,
                constraints=manifest.get("constraints"),
                variables=manifest.get("variables"),
                manifest=manifest,
            )

        with file_lock(cache_dir / ".lock"):
            if manifest_path.exists() and r1cs_path.exists() and witness_path.exists():
                manifest = read_json(manifest_path, default={})
                return InputArtifact(
                    case=case,
                    stem=str(manifest.get("stem", stem_for(case.circuit, case.param, case.density))),
                    directory=cache_dir,
                    r1cs=r1cs_path,
                    witness=witness_path,
                    constraints=manifest.get("constraints"),
                    variables=manifest.get("variables"),
                    manifest=manifest,
                )
            return self._generate(case, cache_dir)

    def _generate(self, case: Case, cache_dir: Path) -> InputArtifact:
        if case.param is None:
            raise ValueError("case has no circuit parameter")
        stem = stem_for(case.circuit, case.param, case.density)
        data_dir = ensure_dir(self.config.data_dir_for(case.protocol, case.platform))
        argv = build_gen(self.config, case.protocol, case.platform, case)
        log_dir = ensure_dir(cache_dir / "logs")
        result = run_process(
            argv,
            log_dir=log_dir,
            name="gen",
            cwd=self.config.repo_root,
            timeout_s=int(self.config.get("run", "timeout_s", default=7200)),
        )
        if not result.ok:
            raise RuntimeError(
                f"gen failed for {case.case_id}: {result.returncode}\n"
                f"{result.stderr[-4000:]}"
            )
        source_r1cs = data_dir / f"{stem}.bin"
        source_witness = data_dir / f"{stem}.witness.bin"
        if not source_r1cs.exists() or not source_witness.exists():
            raise RuntimeError(
                f"gen did not produce expected files under {data_dir}: {stem}.bin"
            )
        r1cs_path = cache_dir / "circuit.bin"
        witness_path = cache_dir / "witness.bin"
        shutil.copy2(source_r1cs, r1cs_path)
        shutil.copy2(source_witness, witness_path)
        constraints = _first_int(CONSTRAINTS_RE, result.stdout)
        variables = _first_int(VARIABLES_RE, result.stdout)
        if case.k is not None and constraints is None:
            raise RuntimeError(f"gen did not report constraints for {case.case_id}")
        if case.k is not None and constraints != (1 << case.k):
            raise RuntimeError(
                f"scale mismatch for {case.case_id}: expected 2^{case.k}={1 << case.k}, "
                f"gen reported {constraints}"
            )
        manifest = {
            "protocol": case.protocol,
            "circuit": case.circuit,
            "param": case.param,
            "density": case.density,
            "stem": stem,
            "constraints": constraints,
            "variables": variables,
            "r1cs_sha256": sha256_file(r1cs_path),
            "witness_sha256": sha256_file(witness_path),
            "r1cs_bytes": r1cs_path.stat().st_size,
            "witness_bytes": witness_path.stat().st_size,
            "command": argv,
            "gen_seconds": result.duration_s,
            "generated_at": time.strftime("%Y-%m-%dT%H:%M:%S%z"),
        }
        write_json(cache_dir / "manifest.json", manifest)
        return InputArtifact(
            case=case,
            stem=stem,
            directory=cache_dir,
            r1cs=r1cs_path,
            witness=witness_path,
            constraints=constraints,
            variables=variables,
            manifest=manifest,
        )

    def prepare(
        self, case: Case, workdir: Path, *, force_setup: bool = False
    ) -> tuple[InputArtifact | None, SetupArtifact | None]:
        """Generate/copy inputs and run the per-case setup.

        Setup is intentionally not reused across cases.  Within one case the
        result is reused only when the caller resumes the same case directory.
        """
        if case.is_stark:
            return None, None
        input_artifact = self.ensure(case)
        if input_artifact is None:
            raise RuntimeError("missing input artifact")
        ensure_dir(workdir)
        stem = input_artifact.stem
        r1cs_link = workdir / f"{stem}.bin"
        witness_link = workdir / f"{stem}.witness.bin"
        _link_or_copy(input_artifact.r1cs, r1cs_link)
        _link_or_copy(input_artifact.witness, witness_link)

        setup_manifest_path = workdir / "setup.json"
        pk_path = workdir / f"{stem}.pk.bin"
        vk_path = workdir / f"{stem}.vk.bin"
        srs_path = workdir / f"{stem}.srs.bin"
        if not force_setup and setup_manifest_path.exists() and pk_path.exists() and vk_path.exists():
            manifest = read_json(setup_manifest_path, default={})
            return input_artifact, SetupArtifact(
                case=case,
                stem=stem,
                directory=workdir,
                proving_key=pk_path,
                verification_key=vk_path,
                srs=srs_path if srs_path.exists() else None,
                duration_s=float(manifest.get("duration_s", 0.0)),
                manifest=manifest,
            )

        argv = build_setup(self.config, case.protocol, case.platform, case, workdir, stem)
        result = run_process(
            argv,
            log_dir=ensure_dir(workdir / "logs"),
            name="setup",
            cwd=self.config.repo_root,
            timeout_s=int(self.config.get("run", "timeout_s", default=7200)),
        )
        if not result.ok:
            raise RuntimeError(
                f"setup failed for {case.case_id}: {result.returncode}\n"
                f"{result.stderr[-4000:]}"
            )
        if not pk_path.exists() or not vk_path.exists():
            raise RuntimeError(f"setup did not produce PK/VK under {workdir}")
        manifest = {
            "case_id": case.case_id,
            "stem": stem,
            "command": argv,
            "duration_s": result.duration_s,
            "proving_key_sha256": sha256_file(pk_path),
            "verification_key_sha256": sha256_file(vk_path),
            "proving_key_bytes": pk_path.stat().st_size,
            "verification_key_bytes": vk_path.stat().st_size,
            "srs_sha256": sha256_file(srs_path) if srs_path.exists() else None,
        }
        write_json(setup_manifest_path, manifest)
        return input_artifact, SetupArtifact(
            case=case,
            stem=stem,
            directory=workdir,
            proving_key=pk_path,
            verification_key=vk_path,
            srs=srs_path if srs_path.exists() else None,
            duration_s=result.duration_s,
            manifest=manifest,
        )


def _first_int(pattern: re.Pattern[str], text: str) -> int | None:
    match = pattern.search(text)
    return int(match.group(1)) if match else None


def _link_or_copy(source: Path, target: Path) -> None:
    if target.exists() or target.is_symlink():
        try:
            if target.resolve() == source.resolve():
                return
        except OSError:
            pass
        target.unlink()
    try:
        os.symlink(source, target)
    except OSError:
        shutil.copy2(source, target)
