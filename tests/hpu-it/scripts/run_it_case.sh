#!/usr/bin/env bash
set -Eeuo pipefail

usage() {
  printf 'Usage: %s /path/to/IT-SCPU-RTL /path/to/HPU_IT_*.elf [/absolute/path/monitor.evidence]\n' \
    "$(basename "$0")" >&2
}

if [[ $# -lt 2 || $# -gt 3 ]]; then
  usage
  exit 2
fi

it_root=$(cd "$1" && pwd -P)
elf=$(cd "$(dirname "$2")" && pwd -P)/$(basename "$2")
runner="$it_root/run_hpu_difftest_elf.sh"
artifact_dir=$(dirname "$elf")
mem_profile="$artifact_dir/.hpu-mem-profile"
source_fingerprint_file="$artifact_dir/.source-fingerprint"
evidence_file=${3:-}

if [[ -n $evidence_file && $evidence_file != /* ]]; then
  printf 'ERROR: monitor evidence path must be absolute: %s\n' "$evidence_file" >&2
  exit 2
fi

[[ -x $runner || -f $runner ]] || {
  printf 'ERROR: missing IT runner: %s\n' "$runner" >&2
  exit 2
}
[[ -s $elf ]] || {
  printf 'ERROR: missing ELF: %s\n' "$elf" >&2
  exit 2
}
[[ -s $mem_profile ]] || {
  printf 'ERROR: missing artifact memory profile: %s\n' "$mem_profile" >&2
  exit 2
}
for tool in find grep wc riscv64-linux-gnu-strings; do
  command -v "$tool" >/dev/null || {
    printf 'ERROR: missing required tool: %s\n' "$tool" >&2
    exit 2
  }
done
for metadata in .source-fingerprint .am-revision .toolchain-identity; do
  if [[ ! -s $artifact_dir/$metadata ]]; then
    printf 'ERROR: incomplete artifact metadata: %s/%s\n' \
      "$artifact_dir" "$metadata" >&2
    exit 2
  fi
done
profile_text=$(<"$mem_profile")
profile_re='^HPU_MEM_BASE=(0[xX][0-9a-fA-F]+|[0-9]+);HPU_MEM_LINES=([1-9][0-9]*);HPU_LINE_BYTES=256;HPU_MEM_BYTES=([1-9][0-9]*)$'
if [[ ! $profile_text =~ $profile_re ]]; then
  printf 'ERROR: malformed artifact memory profile: %s\n' "$profile_text" >&2
  exit 2
fi
hpu_mem_base=${BASH_REMATCH[1]}
hpu_mem_lines=$((10#${BASH_REMATCH[2]}))
profile_mem_bytes=${BASH_REMATCH[3]}
if (( hpu_mem_lines < 19201 ||
      hpu_mem_lines > 36028797018963967 ||
      hpu_mem_lines * 256 != profile_mem_bytes )); then
  printf 'ERROR: inconsistent artifact memory profile: %s\n' "$profile_text" >&2
  exit 2
fi

source_fingerprint=$(<"$source_fingerprint_file")
if [[ ! $source_fingerprint =~ ^[0-9a-f]{16}$ ]]; then
  printf 'ERROR: malformed artifact source fingerprint: %s\n' \
    "$source_fingerprint" >&2
  exit 2
fi
expected_tag="hpu-${hpu_mem_base}-${hpu_mem_lines}-${source_fingerprint}"
if [[ ${artifact_dir##*/} != "$expected_tag" ]]; then
  printf 'ERROR: artifact directory is not a fingerprint publication: %s\n' \
    "$artifact_dir" >&2
  printf '       expected directory name: %s\n' "$expected_tag" >&2
  exit 2
fi

case_id=$(basename "$elf" .elf)
if [[ ! $case_id =~ ^HPU_IT_[A-Z0-9_]+$ ]]; then
  printf 'ERROR: malformed testcase ELF name: %s\n' "$elf" >&2
  exit 2
fi
for suffix in elf bin txt; do
  count=$(find "$artifact_dir" -maxdepth 1 -type f \
    -name "HPU_IT_*.$suffix" | wc -l)
  if (( count != 49 )); then
    printf 'ERROR: artifact publication has %d .%s files, expected 49: %s\n' \
      "$count" "$suffix" "$artifact_dir" >&2
    exit 2
  fi
done
[[ -s $artifact_dir/$case_id.bin && -s $artifact_dir/$case_id.txt ]] || {
  printf 'ERROR: testcase ELF lacks matching bin/txt siblings: %s\n' \
    "$case_id" >&2
  exit 2
}

expected_embedded_profile="HPU_MEM_BASE=$hpu_mem_base;HPU_MEM_LINES=$hpu_mem_lines"
expected_embedded_fingerprint="HPU_SOURCE_FINGERPRINT=$source_fingerprint"
strings_output=$(riscv64-linux-gnu-strings -a "$elf")
for required_string in \
  "$case_id" \
  "$expected_embedded_profile" \
  "$expected_embedded_fingerprint" \
  'HPU_IT_BEGIN case=%s testpoint=%s priority=P%u' \
  'HPU_IT_END case=%s result=%s rc=%d requirements=0x%x'; do
  grep -Fxq "$required_string" <<<"$strings_output" || {
    printf 'ERROR: ELF lacks required embedded identity/marker: %s (%s)\n' \
      "$required_string" "$elf" >&2
    exit 2
  }
done

exec env \
  HPU_MEM_BASE="$hpu_mem_base" \
  HPU_MEM_LINES="$hpu_mem_lines" \
  HPU_IT_EVIDENCE_FILE="$evidence_file" \
  bash "$runner" "$elf"
