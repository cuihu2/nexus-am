#!/usr/bin/env python3
"""显式Poseidon实验接收器；不会注册到默认SEAL清单或GitHub下载包。"""
import argparse
import csv
import importlib.util
import json
from pathlib import Path
import re
import shutil
import sys

ROOT = Path(__file__).resolve().parents[1]
HPU_ROOT = ROOT.parent.parent
sys.path.insert(0, str(HPU_ROOT / "scripts"))
module_spec = importlib.util.spec_from_file_location("hpu_application_core", HPU_ROOT / "scripts/import-application-package.py")
CORE = importlib.util.module_from_spec(module_spec)
module_spec.loader.exec_module(CORE)
with (ROOT / "scheme-cases.tsv").open() as stream:
    CASE_SPECS = {r["program_stem"]: r for r in csv.DictReader(stream, delimiter="\t")}
CORE.CASE_SPECS = CASE_SPECS
require = CORE.require
load_json = CORE.load_json

def validate_poseidon(source, spec, poseidon_commit):
    if not spec["program_stem"].startswith("poseidon_"):
        return None
    report = load_json(source.with_name(source.name + ".poseidon.json"))
    prefix = {"ckks": "EvaluatorCkksBase", "bfv": "EvaluatorBfvBase",
              "bgv": "EvaluatorBgvBase"}[spec["scheme"]]
    api = {"hadd": "add", "hmul": "multiply_relin", "reline": "relinearize",
           "rotate": "rotate" if spec["scheme"] == "ckks" else "rotate_row",
           "modswitch": "rescale" if spec["scheme"] == "ckks" else "drop_modulus_to_next"}[spec["role"]]
    require(bool(poseidon_commit) and report.get("revision") == poseidon_commit and
            re.fullmatch(r"[0-9a-f]{40}", poseidon_commit) and
            report.get("library") == "https://github.com/luhang-HPU/poseidon" and
            report.get("scheme") == spec["scheme"] and report.get("degree") == int(spec["degree"]) and
            report.get("program") == spec["program_stem"] and
            report.get("api") == f"{prefix}::{api}" and report.get("status") == "PASS" and
            report.get("device") == "software" and report.get("key_switch") == "BV/P=1" and
            report.get("comparison") ==
            ("exact decrypted BFV polynomial; SEAL physical golden"
             if spec["scheme"] == "bfv" and spec["role"] == "hmul"
             else "all raw words and ciphertext metadata") and
            report.get("rotation_steps") == (int(spec["degree"]) // 4 if spec["role"] == "rotate" else 0),
            "Poseidon API oracle/provenance mismatch")
    mismatches = report.get("raw_word_mismatches")
    require(type(mismatches) is int and mismatches >= 0 and
            report.get("plaintext_coefficients_compared") ==
            (2 * int(spec["degree"]) if spec["scheme"] == "bfv" and spec["role"] == "hmul" else 0) and
            ((spec["scheme"] == "bfv" and spec["role"] == "hmul") or mismatches == 0),
            "Poseidon comparison evidence is incomplete")
    return report


def import_package(source, destination, validator, producer_commit, encoder, program_model, poseidon_commit):
    case = load_json(source / "package.json")["case_name"]
    require(case in CASE_SPECS, "unknown experimental case")
    report = validate_poseidon(source, CASE_SPECS[case], poseidon_commit)
    CORE.import_package(source, destination, validator, producer_commit, encoder, program_model)
    shutil.copy2(source.with_name(source.name + ".poseidon.json"), destination / "POSEIDON_ORACLE.json")
    path = destination / "AM_ADAPTATION.json"
    adaptation = load_json(path)
    adaptation["poseidon_oracle"] = report
    path.write_text(json.dumps(adaptation, indent=2) + "\n")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    for name in ("source", "destination", "validator", "encoder", "program-model"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--producer-commit", required=True)
    parser.add_argument("--poseidon-commit", required=True)
    args = parser.parse_args()
    require(not args.destination.resolve().is_relative_to(args.source.resolve()) and
            not args.source.resolve().is_relative_to(args.destination.resolve()), "overlapping import")
    import_package(args.source, args.destination, args.validator, args.producer_commit,
                   args.encoder, args.program_model, args.poseidon_commit)
