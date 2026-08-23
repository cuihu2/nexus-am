#!/usr/bin/env bash
set -Eeuo pipefail

script_dir=$(cd "$(dirname "$0")" && pwd)
repo_root=$(cd "$script_dir/.." && pwd)
profile_lib="$script_dir/artifact_profile_lib.sh"
if [[ ! -r $profile_lib ]]; then
  printf 'ERROR: missing artifact profile helper: %s\n' "$profile_lib" >&2
  exit 2
fi
# shellcheck source=artifact_profile_lib.sh
source "$profile_lib"
image_dir=${1:-"$repo_root/build-nexus-am/images"}
requested_hpu_mem_base=${HPU_IT_MEM_BASE:-}
requested_hpu_mem_lines=${HPU_IT_MEM_LINES:-}
requested_source_fingerprint=${HPU_IT_EXPECTED_SOURCE_FINGERPRINT:-}
allow_staging=${HPU_IT_VALIDATE_STAGING:-0}

for tool in readelf objdump objcopy strings; do
  command -v "riscv64-linux-gnu-$tool" >/dev/null || {
    printf 'ERROR: missing riscv64-linux-gnu-%s\n' "$tool" >&2
    exit 2
  }
done
for tool in cmp sha256sum; do
  command -v "$tool" >/dev/null || {
    printf 'ERROR: missing %s\n' "$tool" >&2
    exit 2
  }
done
resolved_relocation_root=$(
  bash "$script_dir/prepare_resolved_relocations.sh" | tail -n 1
)

if [[ -e $image_dir/latest || -L $image_dir/latest ]]; then
  if [[ ! -d $image_dir/latest ]]; then
    printf 'ERROR: latest does not resolve to an image directory: %s/latest\n' \
      "$image_dir" >&2
    exit 2
  fi
  image_dir=$(cd "$image_dir/latest" && pwd -P)
elif [[ -d $image_dir ]]; then
  image_dir=$(cd "$image_dir" && pwd -P)
else
  printf 'ERROR: image directory does not exist: %s\n' "$image_dir" >&2
  exit 2
fi

if [[ ! -f $image_dir/.hpu-mem-profile ]]; then
  printf 'ERROR: HPU memory profile is missing in %s\n' "$image_dir" >&2
  exit 2
fi
mem_profile=$(<"$image_dir/.hpu-mem-profile")
profile_re='^HPU_MEM_BASE=(0[xX][0-9a-fA-F]+|[0-9]+);HPU_MEM_LINES=([1-9][0-9]*);HPU_LINE_BYTES=256;HPU_MEM_BYTES=([1-9][0-9]*)$'
if [[ ! $mem_profile =~ $profile_re ]]; then
  printf 'ERROR: malformed HPU memory profile in %s\n' "$image_dir" >&2
  exit 2
fi
hpu_mem_base=${BASH_REMATCH[1]}
hpu_mem_lines=$((10#${BASH_REMATCH[2]}))
profile_mem_bytes=${BASH_REMATCH[3]}
if (( hpu_mem_lines < 19201 )); then
  printf 'ERROR: HPU_IT_MEM_LINES must be at least 19201 for FHE scratch and guard\n' >&2
  exit 2
fi
if (( hpu_mem_lines > 36028797018963967 )); then
  printf 'ERROR: HPU_IT_MEM_LINES overflows byte-size calculation\n' >&2
  exit 2
fi
expected_mem_profile="HPU_MEM_BASE=$hpu_mem_base;HPU_MEM_LINES=$hpu_mem_lines;HPU_LINE_BYTES=256;HPU_MEM_BYTES=$((hpu_mem_lines * 256))"
if [[ $mem_profile != "$expected_mem_profile" ||
      $profile_mem_bytes != "$((hpu_mem_lines * 256))" ]]; then
  printf 'ERROR: inconsistent HPU memory profile in %s\n' "$image_dir" >&2
  exit 2
fi
if [[ -n $requested_hpu_mem_base ]]; then
  if [[ ! $requested_hpu_mem_base =~ ^(0[xX][0-9a-fA-F]+|[0-9]+)$ ||
        $requested_hpu_mem_base != "$hpu_mem_base" ]]; then
    printf 'ERROR: requested HPU_IT_MEM_BASE=%s, artifact profile is %s\n' \
      "$requested_hpu_mem_base" "$hpu_mem_base" >&2
    exit 2
  fi
fi
if [[ -n $requested_hpu_mem_lines ]]; then
  if [[ ! $requested_hpu_mem_lines =~ ^[1-9][0-9]*$ ]]; then
    printf 'ERROR: requested HPU_IT_MEM_LINES=%s, artifact profile is %s\n' \
      "$requested_hpu_mem_lines" "$hpu_mem_lines" >&2
    exit 2
  fi
  requested_hpu_mem_lines=$((10#$requested_hpu_mem_lines))
  if (( requested_hpu_mem_lines != hpu_mem_lines )); then
    printf 'ERROR: requested HPU_IT_MEM_LINES=%s, artifact profile is %s\n' \
      "$requested_hpu_mem_lines" "$hpu_mem_lines" >&2
    exit 2
  fi
fi
expected_embedded_profile="HPU_MEM_BASE=$hpu_mem_base;HPU_MEM_LINES=$hpu_mem_lines"

mapfile -t case_sources < <(hpu_case_sources "$repo_root")
if [[ ${#case_sources[@]} -ne 49 ]]; then
  printf 'ERROR: expected 49 testcase sources, found %d\n' \
    "${#case_sources[@]}" >&2
  exit 2
fi

for metadata in .source-fingerprint .am-revision .toolchain-identity \
                .qualification-status.csv; do
  if [[ ! -s $image_dir/$metadata ]]; then
    printf 'ERROR: missing fingerprint metadata %s in %s\n' \
      "$metadata" "$image_dir" >&2
    exit 2
  fi
done
if [[ $(wc -l < "$image_dir/.qualification-status.csv") -ne 50 ]]; then
  printf 'ERROR: qualification status must contain header plus 49 rows\n' >&2
  exit 2
fi
for blocked in HPU_IT_DIR_CMB_015; do
  grep -Fxq "$blocked,BLOCKED_EXTERNAL_ALGORITHM_CONTRACT,HPU_commands_not_issued" \
    "$image_dir/.qualification-status.csv" || {
    printf 'ERROR: missing explicit blocked status for %s\n' "$blocked" >&2
    exit 2
  }
done
if [[ $(grep -c ',BLOCKED_EXTERNAL_ALGORITHM_CONTRACT,' \
        "$image_dir/.qualification-status.csv") -ne 1 ]]; then
  printf 'ERROR: qualification status must contain exactly one blocked case\n' >&2
  exit 2
fi
if [[ $(grep -c ',EXECUTABLE_PASS_READY,output_check_authoritative$' \
        "$image_dir/.qualification-status.csv") -ne 48 ]]; then
  printf 'ERROR: qualification status must contain 48 output-authoritative PASS-ready cases\n' >&2
  exit 2
fi
source_fingerprint=$(<"$image_dir/.source-fingerprint")
am_revision=$(<"$image_dir/.am-revision")
compiler_identity=$(<"$image_dir/.toolchain-identity")
if [[ ! $source_fingerprint =~ ^[0-9a-f]{16}$ ]]; then
  printf 'ERROR: malformed source fingerprint in %s\n' "$image_dir" >&2
  exit 2
fi
mapfile -t fingerprint_inputs < <(hpu_fingerprint_inputs "$repo_root")
computed_source_fingerprint=$(hpu_compute_source_fingerprint \
  "$repo_root" "$hpu_mem_base" "$hpu_mem_lines" \
  "$am_revision" "$compiler_identity" "${fingerprint_inputs[@]}")
if [[ $source_fingerprint != "$computed_source_fingerprint" ]]; then
  printf 'ERROR: source fingerprint is stale: stored=%s current=%s\n' \
    "$source_fingerprint" "$computed_source_fingerprint" >&2
  exit 2
fi
if [[ -n $requested_source_fingerprint &&
      $requested_source_fingerprint != "$source_fingerprint" ]]; then
  printf 'ERROR: requested source fingerprint=%s, artifact fingerprint is %s\n' \
    "$requested_source_fingerprint" "$source_fingerprint" >&2
  exit 2
fi
expected_tag="hpu-${hpu_mem_base}-${hpu_mem_lines}-${source_fingerprint}"
image_dir_name=${image_dir##*/}
if [[ $image_dir_name != "$expected_tag" ]]; then
  if [[ $allow_staging != 1 ||
        $image_dir_name != .staging-${expected_tag}.* ]]; then
    printf 'ERROR: image directory tag does not match sidecar/fingerprint: %s (expected %s)\n' \
      "$image_dir_name" "$expected_tag" >&2
    exit 2
  fi
fi
expected_embedded_fingerprint="HPU_SOURCE_FINGERPRINT=$source_fingerprint"

for suffix in elf bin txt; do
  count=$(find "$image_dir" -maxdepth 1 -type f -name "HPU_IT_*.$suffix" | wc -l)
  if [[ $count -ne 49 ]]; then
    printf 'ERROR: expected 49 .%s artifacts, found %d in %s\n' \
      "$suffix" "$count" "$image_dir" >&2
    exit 2
  fi
done

# Re-generating all 49 binary and disassembly files needs hundreds of MiB.
# Keep that scratch space beside the resolved artifact directory by default;
# small system /tmp mounts must not make an otherwise valid publication fail.
# Callers can still select another filesystem explicitly.
validation_tmp_parent=${HPU_IT_VALIDATE_TMPDIR:-${TMPDIR:-${image_dir%/*}}}
if [[ ! -d $validation_tmp_parent || ! -w $validation_tmp_parent ]]; then
  printf 'ERROR: artifact validation temporary directory is not writable: %s\n' \
    "$validation_tmp_parent" >&2
  exit 2
fi
validation_tmp=$(mktemp -d \
  "$validation_tmp_parent/.validate-artifacts.XXXXXXXX")
cleanup_validation_tmp() {
  find "$validation_tmp" -mindepth 1 -delete
  rmdir "$validation_tmp"
}
trap cleanup_validation_tmp EXIT

for case_source in "${case_sources[@]}"; do
  case_id=$(basename "$case_source" .c)
  source_path="$repo_root/$case_source"
  elf="$image_dir/$case_id.elf"
  bin="$image_dir/$case_id.bin"
  txt="$image_dir/$case_id.txt"

  [[ -s $elf && -s $bin && -s $txt ]] || {
    printf 'ERROR: incomplete artifacts for %s\n' "$case_id" >&2
    exit 2
  }
  [[ $(grep -c 'HPU_DEFINE_TESTCASE' "$source_path") -eq 1 ]] || {
    printf 'ERROR: %s must define exactly one testcase descriptor\n' \
      "$case_source" >&2
    exit 2
  }
  grep -Fq "\"$case_id\"" "$source_path" || {
    printf 'ERROR: descriptor ID does not match filename: %s\n' \
      "$case_source" >&2
    exit 2
  }
  case_kind=$(sed -n -E \
    's/^[[:space:]]*(HPU_CASE_[A-Z0-9_]+),[[:space:]]*$/\1/p' \
    "$source_path")
  if [[ ! $case_kind =~ ^HPU_CASE_[A-Z0-9_]+$ ]]; then
    printf 'ERROR: cannot resolve one testcase kind from %s: %s\n' \
      "$case_source" "$case_kind" >&2
    exit 2
  fi

  header=$(riscv64-linux-gnu-readelf -h "$elf")
  grep -Fq 'Class:                             ELF64' <<<"$header" || {
    printf 'ERROR: not ELF64: %s\n' "$elf" >&2
    exit 2
  }
  grep -Fq 'Machine:                           RISC-V' <<<"$header" || {
    printf 'ERROR: not RISC-V: %s\n' "$elf" >&2
    exit 2
  }
  grep -Fq 'Entry point address:               0x80000000' <<<"$header" || {
    printf 'ERROR: unexpected entry point: %s\n' "$elf" >&2
    exit 2
  }

  strings_output=$(riscv64-linux-gnu-strings -a "$elf")
  for required_string in \
    "$case_id" \
    "$expected_embedded_profile" \
    "$expected_embedded_fingerprint" \
    'HPU_IT_BEGIN case=%s testpoint=%s priority=P%u' \
    'HPU_IT_END case=%s result=%s rc=%d requirements=0x%x' \
    "HPU_COMPILED_CASE_KIND=$case_kind"; do
    grep -Fxq "$required_string" <<<"$strings_output" || {
      printf 'ERROR: ELF lacks required embedded identity/marker: %s (%s)\n' \
        "$required_string" "$elf" >&2
      exit 2
    }
  done

  mapfile -t load_segments < <(
    riscv64-linux-gnu-readelf -W -l "$elf" | awk '$1 == "LOAD" { print $3, $4, $5, $6 }'
  )
  if [[ ${#load_segments[@]} -ne 1 ||
        ${load_segments[0]} != 0x0000000080000000\ 0x0000000080000000\ * ]]; then
    printf 'ERROR: expected one DRAM LOAD at 0x80000000: %s\n' "$elf" >&2
    exit 2
  fi

  disassembly=$(riscv64-linux-gnu-objdump -d "$elf")
  required_words=()
  case "$case_kind" in
    HPU_CASE_PATH_CUSTOM1)
      required_words=(00b5102b 00b51e2b 00b5502b 00b55e2b 7000000b) ;;
    HPU_CASE_INS_PADD) required_words=(0400400b 7000000b) ;;
    HPU_CASE_INS_PSUB) required_words=(1400400b 7000000b) ;;
    HPU_CASE_INS_PMUL)
      required_words=(2400400b 2400010b 243fc10b 2040800b 7000000b)
      grep -Fq '<hpu_program_mm>:' <<<"$disassembly" || {
        printf 'ERROR: %s does not contain the generated inline-asm MM entry\n' \
          "$elf" >&2
        exit 2
      }
      ;;
    HPU_CASE_INS_PMAC)
      required_words=(3400400b 3400010b 3400410b 343fc10b 7000000b) ;;
    HPU_CASE_INS_PNTT_STAGE|HPU_CASE_CMB_NTT|HPU_CASE_PERF_NTT)
      required_words=(40402c0b 7000000b) ;;
    HPU_CASE_INS_PINTT_STAGE|HPU_CASE_CMB_INTT|HPU_CASE_PERF_INTT)
      required_words=(50402c0b 7000000b) ;;
    HPU_CASE_INS_PMODLD) required_words=(6000000b 6001800b 7000000b) ;;
    HPU_CASE_INS_PSYNC) required_words=(7000000b) ;;
    HPU_CASE_INS_PFREE) required_words=(8000000b 81c0000b 7000000b) ;;
    HPU_CASE_CMB_BCONV|HPU_CASE_PERF_BCONV)
      required_words=(2400400b 3400400b 7000000b) ;;
    HPU_CASE_CMB_KEYSWITCH|HPU_CASE_PERF_KEYSWITCH)
      required_words=(40c02c0b 50c02c0b 7000000b)
      grep -Fq '<hpu_program_keyswitch>:' <<<"$disassembly" || {
        printf 'ERROR: %s does not contain generated KeySwitch entry\n' "$elf" >&2
        exit 2
      }
      ;;
    HPU_CASE_CMB_RELINE|HPU_CASE_PERF_RELIN)
      required_words=(40c02c0b 50c02c0b 7000000b)
      grep -Fq '<hpu_program_relinearization>:' <<<"$disassembly" || {
        printf 'ERROR: %s does not contain generated Relinearization entry\n' "$elf" >&2
        exit 2
      }
      ;;
    HPU_CASE_CMB_HMUL|HPU_CASE_PERF_CIPHERTEXT_MUL|HPU_CASE_APP_MINI_FHE)
      required_words=(40c02c0b 50c02c0b 7000000b)
      grep -Fq '<hpu_program_ciphertext_multiply>:' <<<"$disassembly" || {
        printf 'ERROR: %s does not contain generated CiphertextMultiply entry\n' "$elf" >&2
        exit 2
      }
      ;;
    HPU_CASE_CMB_NTT_AUTO|HPU_CASE_CMB_ROTATE)
      required_words=(40c02c0b 50c02c0b 7000000b)
      grep -Fq '<hpu_program_auto>:' <<<"$disassembly" || {
        printf 'ERROR: %s does not contain generated Auto entry\n' "$elf" >&2
        exit 2
      }
      ;;
    HPU_CASE_CMB_ENCODE)
      required_words=(2000c00b 40c0000b 7000000b)
      grep -Fq '<hpu_program_encode>:' <<<"$disassembly" || {
        printf 'ERROR: %s does not contain generated Encode entry\n' "$elf" >&2
        exit 2
      }
      ;;
    HPU_CASE_CMB_RESCALE)
      required_words=(0000400b 1000400b 2000400b 7000000b)
      grep -Fq '<hpu_program_rescale>:' <<<"$disassembly" || {
        printf 'ERROR: %s does not contain generated Rescale entry\n' "$elf" >&2
        exit 2
      }
      ;;
  esac

  expected_dma_manifest=
  case "$case_id" in
    HPU_IT_DIR_INS_C0_003) expected_dma_manifest=mm.csv ;;
    HPU_IT_DIR_CMB_005|HPU_IT_DIR_CMB_014) expected_dma_manifest=auto.csv ;;
    HPU_IT_DIR_CMB_011) expected_dma_manifest=encode.csv ;;
    HPU_IT_DIR_CMB_013) expected_dma_manifest=rescale.csv ;;
    HPU_IT_DIR_CMB_004|HPU_IT_DIR_PERF_005)
      expected_dma_manifest=keyswitch.csv ;;
    HPU_IT_DIR_CMB_012|HPU_IT_DIR_PERF_001)
      expected_dma_manifest=relinearization.csv ;;
    HPU_IT_DIR_CMB_010|HPU_IT_DIR_PERF_006|HPU_IT_DIR_APP_001)
      expected_dma_manifest=ciphertext_multiply.csv ;;
  esac
  if [[ -n $expected_dma_manifest ]]; then
    dma_manifest="$image_dir/$case_id.dma.csv"
    [[ -s $dma_manifest ]] || {
      printf 'ERROR: missing resolved DMA manifest for %s\n' "$case_id" >&2
      exit 2
    }
    cmp -s "$resolved_relocation_root/$expected_dma_manifest" \
      "$dma_manifest" || {
      printf 'ERROR: stale resolved DMA manifest for %s\n' "$case_id" >&2
      exit 2
    }
    if grep -Fq 'unclassified' "$dma_manifest" ||
       tail -n +2 "$dma_manifest" | grep -Evq ',RESOLVED$'; then
      printf 'ERROR: unresolved DMA row for %s\n' "$case_id" >&2
      exit 2
    fi
    if grep -B3 -E '\.word[[:space:]]+0x00b5[45][0-9a-f]2b' \
         <<<"$disassembly" | grep -Eq 'li[[:space:]]+a1,0([[:space:]]|$)'; then
      printf 'ERROR: %s loads zero into x11/a1 before generated DSTORE\n' \
        "$case_id" >&2
      exit 2
    fi
  fi
  for word in "${required_words[@]}"; do
    grep -Fq "$word" <<<"$disassembly" || {
      printf 'ERROR: %s (%s) lacks path-specific instruction word 0x%s\n' \
        "$elf" "$case_kind" "$word" >&2
      exit 2
    }
  done

  case "$case_kind" in
    HPU_CASE_CMB_BOOTSTRAP)
      grep -Fq 'hpu_commands=not_issued' <<<"$strings_output" || {
        printf 'ERROR: unresolved algorithm case lacks explicit non-execution marker: %s\n' \
          "$elf" >&2
        exit 2
      }
      ;;
    *)
      if grep -Fq 'hpu_commands=not_issued' <<<"$strings_output"; then
        printf 'ERROR: supported testcase retained a no-command route: %s\n' \
          "$elf" >&2
        exit 2
      fi
      ;;
  esac

  regenerated_bin="$validation_tmp/$case_id.bin"
  riscv64-linux-gnu-objcopy -S \
    --set-section-flags .bss=alloc,contents -O binary \
    "$elf" "$regenerated_bin"
  cmp -s "$regenerated_bin" "$bin" || {
    printf 'ERROR: .bin was not generated from its matching ELF: %s\n' \
      "$bin" >&2
    exit 2
  }

  grep -Fq "$case_id.elf" "$txt" &&
    grep -Fq 'file format elf64-littleriscv' "$txt" &&
    grep -Fq 'Disassembly of section .text:' "$txt" &&
    grep -Eq '^[[:space:]]*80000000:' "$txt" || {
      printf 'ERROR: malformed or mismatched objdump text: %s\n' "$txt" >&2
      exit 2
    }

  regenerated_txt="$validation_tmp/$case_id.txt"
  (
    cd "$image_dir"
    riscv64-linux-gnu-objdump -d "$case_id.elf"
  ) > "$regenerated_txt"
  cmp -s "$regenerated_txt" "$txt" || {
    printf 'ERROR: .txt was not generated from its matching ELF: %s\n' \
      "$txt" >&2
    exit 2
  }
done

printf 'PASS: validated 49 testcase descriptors and 49 RISC-V ELF/bin/txt sets in %s\n' \
  "$image_dir"
