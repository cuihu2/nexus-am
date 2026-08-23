#!/usr/bin/env bash
set -Eeuo pipefail

script_dir=$(cd "$(dirname "$0")" && pwd)
repo_root=$(cd "$script_dir/.." && pwd)
# shellcheck source=artifact_profile_lib.sh
source "$script_dir/artifact_profile_lib.sh"
image_dir=${1:-"$repo_root/build/generated-operators/latest"}
allow_staging=${HPU_IT_VALIDATE_STAGING:-0}
requested_fingerprint=${HPU_IT_EXPECTED_SOURCE_FINGERPRINT:-}
resolved_relocation_root=$(
  bash "$script_dir/prepare_resolved_relocations.sh" | tail -n 1
)

if [[ -e $image_dir/latest || -L $image_dir/latest ]]; then
  image_dir=$(cd "$image_dir/latest" && pwd -P)
else
  image_dir=$(cd "$image_dir" && pwd -P)
fi
for metadata in .hpu-mem-profile .source-fingerprint .am-revision \
                .toolchain-identity .qualification-status.csv; do
  [[ -s $image_dir/$metadata ]] || {
    printf 'ERROR: missing %s in %s\n' "$metadata" "$image_dir" >&2
    exit 2
  }
done
[[ $(wc -l < "$image_dir/.qualification-status.csv") -eq 5 ]] || exit 2
if grep -Fq ',BLOCKED_' "$image_dir/.qualification-status.csv"; then
  printf 'ERROR: generated operator suite contains a blocked case\n' >&2
  exit 2
fi
if [[ $(grep -c ',EXECUTABLE_PASS_READY,output_check_authoritative$' \
        "$image_dir/.qualification-status.csv") -ne 4 ]]; then
  printf 'ERROR: all generated operators must be output-authoritative PASS-ready\n' >&2
  exit 2
fi
mem_profile=$(<"$image_dir/.hpu-mem-profile")
profile_re='^HPU_MEM_BASE=(0[xX][0-9a-fA-F]+|[0-9]+);HPU_MEM_LINES=([1-9][0-9]*);HPU_LINE_BYTES=256;HPU_MEM_BYTES=([1-9][0-9]*)$'
[[ $mem_profile =~ $profile_re ]] || {
  printf 'ERROR: malformed HPU memory profile\n' >&2
  exit 2
}
hpu_mem_base=${BASH_REMATCH[1]}
hpu_mem_lines=$((10#${BASH_REMATCH[2]}))
[[ ${BASH_REMATCH[3]} == "$((hpu_mem_lines * 256))" ]] || exit 2
source_fingerprint=$(<"$image_dir/.source-fingerprint")
am_revision=$(<"$image_dir/.am-revision")
compiler_identity=$(<"$image_dir/.toolchain-identity")
mapfile -t fingerprint_inputs < <(hpu_fingerprint_inputs "$repo_root")
computed=$(hpu_compute_source_fingerprint \
  "$repo_root" "$hpu_mem_base" "$hpu_mem_lines" \
  "$am_revision" "$compiler_identity" "${fingerprint_inputs[@]}")
[[ $computed == "$source_fingerprint" &&
    ( -z $requested_fingerprint || $requested_fingerprint == "$computed" ) ]] || {
  printf 'ERROR: stale generated-operator fingerprint\n' >&2
  exit 2
}
expected_tag="hpu-${hpu_mem_base}-${hpu_mem_lines}-${source_fingerprint}"
name=${image_dir##*/}
if [[ $name != "$expected_tag" &&
      ( $allow_staging != 1 || $name != .staging-${expected_tag}.* ) ]]; then
  printf 'ERROR: generated-operator directory tag mismatch\n' >&2
  exit 2
fi

mapfile -t sources < <(hpu_generated_operator_sources "$repo_root")
[[ ${#sources[@]} -eq 4 ]] || exit 2
for suffix in elf bin txt; do
  count=$(find "$image_dir" -maxdepth 1 -type f -name "HPU_IT_GEN_*.$suffix" | wc -l)
  [[ $count -eq 4 ]] || {
    printf 'ERROR: expected 4 generated .%s files, found %d\n' "$suffix" "$count" >&2
    exit 2
  }
done

scratch=$(mktemp -d "${image_dir%/*}/.validate-generated.XXXXXXXX")
trap 'find "$scratch" -mindepth 1 -delete; rmdir "$scratch"' EXIT
for source in "${sources[@]}"; do
  case_id=$(basename "$source" .c)
  case_kind=$(sed -n -E \
    's/^[[:space:]]*(HPU_CASE_[A-Z0-9_]+),[[:space:]]*$/\1/p' \
    "$repo_root/$source")
  elf="$image_dir/$case_id.elf"
  bin="$image_dir/$case_id.bin"
  txt="$image_dir/$case_id.txt"
  dma_manifest="$image_dir/$case_id.dma.csv"
  [[ -s $elf && -s $bin && -s $txt && -s $dma_manifest ]] || exit 2
  header=$(riscv64-linux-gnu-readelf -h "$elf")
  grep -Fq 'Class:                             ELF64' <<<"$header"
  grep -Fq 'Machine:                           RISC-V' <<<"$header"
  grep -Fq 'Entry point address:               0x80000000' <<<"$header"
  strings_output=$(riscv64-linux-gnu-strings -a "$elf")
  for marker in "$case_id" "HPU_COMPILED_CASE_KIND=$case_kind" \
    "HPU_SOURCE_FINGERPRINT=$source_fingerprint" \
    "HPU_MEM_BASE=$hpu_mem_base;HPU_MEM_LINES=$hpu_mem_lines"; do
    grep -Fxq "$marker" <<<"$strings_output" || {
      printf 'ERROR: %s lacks %s\n' "$case_id" "$marker" >&2
      exit 2
    }
  done
  if grep -Fq 'hpu_commands=not_issued' <<<"$strings_output"; then
    printf 'ERROR: %s contains a no-command route\n' "$case_id" >&2
    exit 2
  fi
  disassembly=$(riscv64-linux-gnu-objdump -d "$elf")
  case "$case_kind" in
    HPU_CASE_GENERATED_PMULT)
      symbol=hpu_program_pmult; words=(2400400b 7000000b) ;;
    HPU_CASE_GENERATED_CMULT)
      symbol=hpu_program_cmult; words=(2400400b 3400400b 7000000b) ;;
    HPU_CASE_GENERATED_MODUP)
      symbol=hpu_program_modup; words=(2000400b 3400400b 7000000b) ;;
    HPU_CASE_GENERATED_MODDOWN)
      symbol=hpu_program_moddown; words=(1000400b 2000800b 7000000b) ;;
    *) exit 2 ;;
  esac
  operator=${case_id#HPU_IT_GEN_}
  operator=${operator%_001}
  operator=${operator,,}
  cmp -s "$resolved_relocation_root/$operator.csv" \
    "$dma_manifest" || {
    printf 'ERROR: stale DMA relocation manifest for %s\n' "$case_id" >&2
    exit 2
  }
  if grep -Fq 'unclassified' "$dma_manifest" ||
     tail -n +2 "$dma_manifest" | grep -Evq ',RESOLVED$'; then
    printf 'ERROR: unresolved DMA relocation row for %s\n' "$case_id" >&2
    exit 2
  fi
  grep -Fq "<$symbol>:" <<<"$disassembly" || {
    printf 'ERROR: %s lacks generated symbol %s\n' "$case_id" "$symbol" >&2
    exit 2
  }
  for word in "${words[@]}"; do
    grep -Fq "$word" <<<"$disassembly" || {
      printf 'ERROR: %s lacks generated instruction word 0x%s\n' \
        "$case_id" "$word" >&2
      exit 2
    }
  done
  grep -Fq '00b5' <<<"$disassembly" || {
    printf 'ERROR: %s lacks custom1 DMA words\n' "$case_id" >&2
    exit 2
  }
  if grep -B3 -E '\.word[[:space:]]+0x00b5[45][0-9a-f]2b' \
       <<<"$disassembly" | grep -Eq 'li[[:space:]]+a1,0([[:space:]]|$)'; then
    printf 'ERROR: %s loads zero into x11/a1 before generated DSTORE\n' \
      "$case_id" >&2
    exit 2
  fi
  riscv64-linux-gnu-objcopy -S --set-section-flags .bss=alloc,contents \
    -O binary "$elf" "$scratch/$case_id.bin"
  cmp -s "$scratch/$case_id.bin" "$bin" || exit 2
  (cd "$image_dir" && riscv64-linux-gnu-objdump -d "$case_id.elf") \
    > "$scratch/$case_id.txt"
  cmp -s "$scratch/$case_id.txt" "$txt" || exit 2
done
printf 'PASS: validated 4 generated operator ELF/bin/txt sets in %s\n' "$image_dir"
