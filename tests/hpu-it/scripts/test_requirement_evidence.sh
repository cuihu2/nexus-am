#!/usr/bin/env bash
set -Eeuo pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
checker="$script_dir/check_requirement_evidence.sh"
scratch=$(mktemp -d "${TMPDIR:-/tmp}/hpu-evidence-test.XXXXXXXX")
trap 'rm -rf -- "$scratch"' EXIT

run_expect() {
  local expected_rc=$1
  local name=$2
  local log=$3
  local actual_rc
  set +e
  "$checker" HPU_IT_DIR_STR_002 0xa13 "$log" >"$scratch/$name.out" 2>&1
  actual_rc=$?
  set -e
  if [[ $actual_rc -ne $expected_rc ]]; then
    printf 'FAIL %-22s expected_rc=%u actual_rc=%u\n' \
      "$name" "$expected_rc" "$actual_rc" >&2
    sed -n '1,20p' "$scratch/$name.out" >&2
    exit 1
  fi
  printf 'PASS %-22s rc=%u\n' "$name" "$actual_rc"
}

printf '%s\n' \
  'HPU_IT_EVIDENCE_V1 case=HPU_IT_DIR_STR_002 producer=uvm.structural run_id=run-001' \
  'HPU_IT_REQ_PASS case=HPU_IT_DIR_STR_002 requirements=0x3 source=uvm.ready' \
  'HPU_IT_REQ_PASS case=HPU_IT_DIR_STR_002 requirements=0x210 source=bind.cache-cover' \
  'HPU_IT_REQ_PASS case=HPU_IT_DIR_STR_002 requirements=0x800 source=rtl-monitor.queue' \
  >"$scratch/complete.log"
run_expect 0 complete "$scratch/complete.log"

printf '%s\n' \
  'HPU_IT_EVIDENCE_V1 case=HPU_IT_DIR_STR_002 producer=uvm.structural run_id=run-002' \
  'HPU_IT_REQ_PASS case=HPU_IT_DIR_STR_002 requirements=0x213 source=uvm.ingress' \
  >"$scratch/missing.log"
run_expect 77 missing-mask "$scratch/missing.log"

printf '%s\n' \
  'HPU_IT_EVIDENCE_V1 case=HPU_IT_DIR_STR_002 producer=uvm.structural run_id=run-003' \
  'HPU_IT_REQ_PASS case=HPU_IT_DIR_STR_006 requirements=0xa13 source=uvm.wrong-case' \
  >"$scratch/wrong-case.log"
run_expect 78 wrong-case "$scratch/wrong-case.log"

printf '%s\n' \
  'HPU_IT_EVIDENCE_V1 case=HPU_IT_DIR_STR_002 producer=uvm.structural run_id=run-004' \
  'HPU_IT_REQ_PASS case=HPU_IT_DIR_STR_002 requirements=0x1a13 source=uvm.overclaim' \
  >"$scratch/undeclared.log"
run_expect 78 undeclared-bit "$scratch/undeclared.log"

printf '%s\n' \
  'HPU_IT_EVIDENCE_V1 case=HPU_IT_DIR_STR_002 producer=uvm.structural run_id=run-005' \
  'HPU_IT_REQ_PASS case=HPU_IT_DIR_STR_002 requirements=0xa13' \
  >"$scratch/malformed.log"
run_expect 78 malformed "$scratch/malformed.log"

printf '%s\n' \
  'HPU_IT_REQ_PASS case=HPU_IT_DIR_STR_002 requirements=0xa13 source=uvm.fake-from-guest-uart' \
  >"$scratch/guest-sim.log"
printf '%s\n' \
  'HPU_IT_EVIDENCE_V1 case=HPU_IT_DIR_STR_002 producer=rtl-monitor.queue run_id=run-006' \
  'HPU_IT_LOCAL_EVIDENCE scope=hpu_cmd_queue depth=8 qualification=external_required' \
  >"$scratch/local-only.log"
run_expect 77 guest-log-is-ignored "$scratch/local-only.log"

printf '%s\n' \
  'HPU_IT_EVIDENCE_V1 case=HPU_IT_DIR_STR_002 producer=guest-uart run_id=run-007' \
  'HPU_IT_REQ_PASS case=HPU_IT_DIR_STR_002 requirements=0xa13 source=guest-uart' \
  >"$scratch/untrusted.log"
run_expect 78 untrusted-source "$scratch/untrusted.log"

printf 'HPU_IT_EVIDENCE_PROTOCOL_SELFTEST_PASS cases=7\n'
