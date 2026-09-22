#!/usr/bin/env python3
"""结构校验并暂存同批 producer 的原始 KeySwitch 包，不宣称语义接入完成。"""

from __future__ import annotations

import argparse
import csv
import json
from pathlib import Path
import re
import shutil
import tempfile


LINE_BYTES = 256
REQUIRED_FILES = (
    "keyswitch.c",
    "keyswitch.h",
    "keyswitch.asm",
    "keyswitch.inst32",
    "keyswitch.cmd26",
    "dma_relocation_manifest.csv",
    "test_data/params.json",
    "test_data/artifact_manifest.csv",
    "test_data/dma_plan.csv",
    "test_data/input_base_q.bin",
    "test_data/input_t2_q.bin",
    "test_data/rlk_ntt_qp.bin",
    "test_data/expected_q.bin",
    "test_data/hardware/abi.json",
    "test_data/hardware/hardware_manifest.csv",
    "test_data/hardware/hpu_mem_config.json",
    "test_data/hardware/hpu_mem_image.u32.bin",
    "test_data/hardware/line_map.csv",
    "test_data/hardware/mod_ctx_map.csv",
    "test_data/hardware/twiddle_map.csv",
    "test_data/hardware/constants/mod_ctx.u32.bin",
)


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)


def rows(path: Path) -> list[dict[str, str]]:
    with path.open(encoding="utf-8", newline="") as stream:
        return list(csv.DictReader(stream))


def require_files(source: Path) -> None:
    for relative in REQUIRED_FILES:
        path = source / relative
        require(path.is_file() and path.stat().st_size > 0,
                f"missing or empty KeySwitch producer file: {relative}")


def validate(source: Path) -> dict[str, int]:
    """Check that the producer emitted one self-consistent raw package.

    This intentionally stops before AM-specific checksum, geometry, instruction,
    and fixture validation.  Those checks belong to the semantic importer.
    """
    require_files(source)
    data = source / "test_data"
    hardware = data / "hardware"
    params = json.loads((data / "params.json").read_text(encoding="utf-8"))
    require(params.get("operation") == "keyswitch",
            "params.json does not describe the KeySwitch package")

    plan = rows(data / "dma_plan.csv")
    relocations = rows(source / "dma_relocation_manifest.csv")
    require(plan, "KeySwitch DMA plan is empty")
    require(len(plan) == len(relocations),
            "KeySwitch DMA plan and relocation manifest row counts differ")
    for index, (span, relocation) in enumerate(zip(plan, relocations)):
        for field in ("instruction_index", "dma_index", "direction"):
            require(span.get(field) == relocation.get(field),
                    f"KeySwitch DMA {index}: {field} differs between manifests")
        require(span.get("object_slot") == relocation.get("obj_id"),
                f"KeySwitch DMA {index}: object slot differs between manifests")
        require(span.get("status") == "RESOLVED",
                f"KeySwitch DMA {index}: span is not RESOLVED")
        require(int(span.get("line_count", "0"), 0) > 0,
                f"KeySwitch DMA {index}: line count must be positive")

    config = json.loads((hardware / "hpu_mem_config.json").read_text(encoding="utf-8"))
    size_lines = int(config.get("size_lines", 0))
    require(size_lines > 0, "KeySwitch HPU_MEM size_lines must be positive")
    image_bytes = (hardware / "hpu_mem_image.u32.bin").stat().st_size
    require(image_bytes == size_lines * LINE_BYTES,
            "KeySwitch HPU_MEM image size does not match size_lines")

    artifact_rows = rows(data / "artifact_manifest.csv")
    hardware_rows = rows(hardware / "hardware_manifest.csv")
    line_rows = rows(hardware / "line_map.csv")
    require(artifact_rows and hardware_rows and line_rows,
            "KeySwitch artifact, hardware, and line manifests must be non-empty")
    for entry in artifact_rows:
        relative = entry.get("path", "")
        require(relative and (data / relative).is_file(),
                f"KeySwitch artifact manifest references a missing file: {relative}")
    for entry in hardware_rows:
        relative = entry.get("path", "")
        require(relative and (hardware / relative).is_file(),
                f"KeySwitch hardware manifest references a missing file: {relative}")

    return {
        "dma_rows": len(plan),
        "artifact_rows": len(artifact_rows),
        "hardware_rows": len(hardware_rows),
        "size_lines": size_lines,
    }


def publish(source: Path, destination: Path, producer_commit: str,
            summary: dict[str, int]) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    if destination.exists():
        require(destination.is_dir() and not destination.is_symlink(),
                "KeySwitch staging destination must be a real directory")
        marker = destination / "STAGING_STATUS.json"
        require(marker.is_file(),
                "refusing to replace an unowned KeySwitch staging destination")
        ownership = json.loads(marker.read_text(encoding="utf-8"))
        require(ownership.get("case") == "keyswitch"
                and ownership.get("source_package") == "outputs/keyswitch",
                "refusing to replace a staging directory with the wrong owner marker")
    staging = Path(tempfile.mkdtemp(prefix=".keyswitch-source-", dir=destination.parent))
    try:
        shutil.copytree(source, staging / "upstream")
        (staging / "producer_commit.txt").write_text(
            producer_commit + "\n", encoding="ascii")
        (staging / "STAGING_STATUS.json").write_text(
            json.dumps({
                "case": "keyswitch",
                "producer_commit": producer_commit,
                "source_package": "outputs/keyswitch",
                "validation_scope": "producer-package-structure-only",
                "semantic_import": False,
                "qualification": "not-evaluated",
                **summary,
            }, indent=2, sort_keys=True) + "\n",
            encoding="utf-8")
        if destination.exists():
            shutil.rmtree(destination)
        staging.rename(destination)
    finally:
        if staging.exists():
            shutil.rmtree(staging)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", required=True, type=Path,
                        help="producer outputs/keyswitch directory")
    parser.add_argument("--destination", required=True, type=Path,
                        help="AM generated KeySwitch source staging directory")
    parser.add_argument("--producer-commit", required=True,
                        help="full commit of the producer that emitted this package")
    args = parser.parse_args()
    require(re.fullmatch(r"[0-9a-f]{40}", args.producer_commit) is not None,
            "producer commit must be a full 40-character Git commit")
    source, destination = args.source.resolve(), args.destination.resolve()
    require(source.is_dir(), "KeySwitch producer source directory is missing")
    require(source != destination and source not in destination.parents
            and destination not in source.parents,
            "source and destination must not overlap")
    summary = validate(source)
    publish(source, destination, args.producer_commit, summary)
    print("KeySwitch source package: structurally checked and staged; "
          "semantic AM import remains pending")


if __name__ == "__main__":
    try:
        main()
    except (ValueError, KeyError, OSError, json.JSONDecodeError) as error:
        raise SystemExit(f"KeySwitch package staging failed: {error}") from error
