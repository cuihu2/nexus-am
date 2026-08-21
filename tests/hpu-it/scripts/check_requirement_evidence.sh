#!/usr/bin/env bash
set -Eeuo pipefail

usage() {
  printf 'Usage: %s CASE_ID EXPECTED_REQUIREMENTS EVIDENCE_FILE\n' "$(basename "$0")" >&2
  printf 'The evidence file must be written by an external bind/UVM/RTL monitor; never pass the guest simulator/UART log.\n' >&2
}

if [[ $# -ne 3 ]]; then
  usage
  exit 2
fi

expected_case=$1
expected_text=$2
log_path=$3
known_requirement_mask=$((0x1fff))
source_re='^(bind|uvm|rtl-monitor)([._-][A-Za-z0-9_.-]+)?$'

[[ $expected_case =~ ^[A-Za-z0-9_.-]+$ ]] || {
  printf 'HPU_IT_EVIDENCE_CHECK status=ERROR reason=invalid-case case=%s\n' \
    "$expected_case" >&2
  exit 78
}
[[ $expected_text =~ ^0[xX][0-9a-fA-F]{1,8}$ ]] || {
  printf 'HPU_IT_EVIDENCE_CHECK status=ERROR reason=invalid-expected-mask mask=%s\n' \
    "$expected_text" >&2
  exit 78
}
[[ -f $log_path ]] || {
  printf 'HPU_IT_EVIDENCE_CHECK status=ERROR reason=missing-evidence-file path=%s\n' \
    "$log_path" >&2
  exit 78
}

expected_hex=${expected_text:2}
expected_requirements=$((16#$expected_hex))
if (( (expected_requirements & ~known_requirement_mask) != 0 )); then
  printf 'HPU_IT_EVIDENCE_CHECK status=ERROR reason=unknown-expected-bits expected=0x%x known=0x%x\n' \
    "$expected_requirements" "$known_requirement_mask" >&2
  exit 78
fi

all_header_count=$(grep -aEc '^HPU_IT_EVIDENCE_V1([[:space:]]|$)' "$log_path" || true)
header_re='^HPU_IT_EVIDENCE_V1[[:space:]]+case=([A-Za-z0-9_.-]+)[[:space:]]+producer=([^[:space:]]+)[[:space:]]+run_id=([A-Za-z0-9_.:-]+)[[:space:]]*$'
valid_header_count=$(grep -aEc "$header_re" "$log_path" || true)
if [[ $all_header_count -ne 1 || $valid_header_count -ne 1 ]]; then
  printf 'HPU_IT_EVIDENCE_CHECK status=ERROR reason=invalid-provenance-header total=%u valid=%u\n' \
    "$all_header_count" "$valid_header_count" >&2
  exit 78
fi
header=$(grep -aE "$header_re" "$log_path")
[[ $header =~ $header_re ]]
header_case=${BASH_REMATCH[1]}
header_producer=${BASH_REMATCH[2]}
if [[ $header_case != "$expected_case" ]]; then
  printf 'HPU_IT_EVIDENCE_CHECK status=ERROR reason=header-case-mismatch expected=%s observed=%s\n' \
    "$expected_case" "$header_case" >&2
  exit 78
fi
if [[ ! $header_producer =~ $source_re ]]; then
  printf 'HPU_IT_EVIDENCE_CHECK status=ERROR reason=untrusted-producer producer=%s\n' \
    "$header_producer" >&2
  exit 78
fi

all_marker_count=$(grep -aEc '^HPU_IT_REQ_PASS([[:space:]]|$)' "$log_path" || true)
marker_re='^HPU_IT_REQ_PASS[[:space:]]+case=([A-Za-z0-9_.-]+)[[:space:]]+requirements=0x([0-9a-fA-F]{1,8})[[:space:]]+source=([^[:space:]]+)[[:space:]]*$'
valid_marker_count=$(grep -aEc "$marker_re" "$log_path" || true)
if [[ $all_marker_count -ne $valid_marker_count ]]; then
  printf 'HPU_IT_EVIDENCE_CHECK status=ERROR reason=malformed-record total=%u valid=%u\n' \
    "$all_marker_count" "$valid_marker_count" >&2
  exit 78
fi

observed_requirements=0
while IFS= read -r marker; do
  [[ $marker =~ $marker_re ]] || {
    printf 'HPU_IT_EVIDENCE_CHECK status=ERROR reason=malformed-record\n' >&2
    exit 78
  }
  record_case=${BASH_REMATCH[1]}
  record_hex=${BASH_REMATCH[2]}
  record_source=${BASH_REMATCH[3]}
  if [[ $record_case != "$expected_case" ]]; then
    printf 'HPU_IT_EVIDENCE_CHECK status=ERROR reason=case-mismatch expected=%s observed=%s source=%s\n' \
      "$expected_case" "$record_case" "$record_source" >&2
    exit 78
  fi
  if [[ ! $record_source =~ $source_re ]]; then
    printf 'HPU_IT_EVIDENCE_CHECK status=ERROR reason=untrusted-source source=%s\n' \
      "$record_source" >&2
    exit 78
  fi
  record_requirements=$((16#$record_hex))
  if (( (record_requirements & ~expected_requirements) != 0 )); then
    printf 'HPU_IT_EVIDENCE_CHECK status=ERROR reason=undeclared-bits source=%s observed=0x%x expected=0x%x\n' \
      "$record_source" "$record_requirements" "$expected_requirements" >&2
    exit 78
  fi
  observed_requirements=$((observed_requirements | record_requirements))
done < <(grep -aE "$marker_re" "$log_path" || true)

if [[ $observed_requirements -eq $expected_requirements ]]; then
  printf 'HPU_IT_EVIDENCE_CHECK status=COMPLETE case=%s observed=0x%x expected=0x%x records=%u\n' \
    "$expected_case" "$observed_requirements" "$expected_requirements" \
    "$valid_marker_count"
  exit 0
fi

missing_requirements=$((expected_requirements & ~observed_requirements))
printf 'HPU_IT_EVIDENCE_CHECK status=INCOMPLETE case=%s missing=0x%x observed=0x%x expected=0x%x records=%u\n' \
  "$expected_case" "$missing_requirements" "$observed_requirements" \
  "$expected_requirements" "$valid_marker_count"
exit 77
