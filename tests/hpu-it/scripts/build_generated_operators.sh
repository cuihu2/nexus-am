#!/usr/bin/env bash
set -Eeuo pipefail

script_dir=$(cd "$(dirname "$0")" && pwd)
repo_root=$(cd "$script_dir/.." && pwd)
# shellcheck source=artifact_profile_lib.sh
source "$script_dir/artifact_profile_lib.sh"
output_root=${1:-"$repo_root/build"}
jobs=${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 1)}
hpu_mem_base=${HPU_IT_MEM_BASE:-0x87000000}
hpu_mem_lines=${HPU_IT_MEM_LINES:-19201}

if [[ -z ${AM_HOME:-} || ! -f ${AM_HOME}/Makefile.app ]]; then
  printf 'ERROR: AM_HOME must point to OpenXiangShan/nexus-am\n' >&2
  exit 2
fi
for build_env in CFLAGS CXXFLAGS CPPFLAGS LDFLAGS ASFLAGS MAKEFLAGS MFLAGS \
                 CC CXX LD AR OBJCOPY OBJDUMP CROSS_COMPILE; do
  if [[ -v $build_env ]]; then
    printf 'ERROR: unset ambient build variable %s before publication\n' \
      "$build_env" >&2
    exit 2
  fi
done
if [[ ! $jobs =~ ^[1-9][0-9]*$ ||
      ! $hpu_mem_base =~ ^(0[xX][0-9a-fA-F]+|[0-9]+)$ ||
      ! $hpu_mem_lines =~ ^[1-9][0-9]*$ ]]; then
  printf 'ERROR: invalid JOBS/HPU memory profile\n' >&2
  exit 2
fi
hpu_mem_lines=$((10#$hpu_mem_lines))
if (( hpu_mem_lines < 19201 )); then
  printf 'ERROR: generated operators require at least 19201 HPU lines\n' >&2
  exit 2
fi
for tool in gcc objcopy objdump readelf strings sha256sum flock git; do
  if [[ $tool == sha256sum || $tool == flock || $tool == git ]]; then
    command -v "$tool" >/dev/null
  else
    command -v "riscv64-linux-gnu-$tool" >/dev/null
  fi
done

am_status=$(git -C "$AM_HOME" status --porcelain=v1 --untracked-files=all \
  -- . ":(exclude)tests/hpu-it")
if [[ -n $am_status ]]; then
  printf 'ERROR: Nexus-AM worktree outside tests/hpu-it must be clean\n' >&2
  exit 2
fi

bash "$script_dir/prepare_inline_vectors.sh"
resolved_relocation_root=$(
  bash "$script_dir/prepare_resolved_relocations.sh" | tail -n 1
)
mapfile -t case_sources < <(hpu_generated_operator_sources "$repo_root")
if [[ ${#case_sources[@]} -ne 4 ]]; then
  printf 'ERROR: expected 4 generated-operator sources, found %d\n' \
    "${#case_sources[@]}" >&2
  exit 2
fi
mapfile -t fingerprint_inputs < <(hpu_fingerprint_inputs "$repo_root")
compiler_identity=$(riscv64-linux-gnu-gcc --version | sed -n '1p')
am_revision=$(git -C "$AM_HOME" rev-parse --verify HEAD)
source_fingerprint=$(hpu_compute_source_fingerprint \
  "$repo_root" "$hpu_mem_base" "$hpu_mem_lines" \
  "$am_revision" "$compiler_identity" "${fingerprint_inputs[@]}")
profile_tag="hpu-${hpu_mem_base}-${hpu_mem_lines}-${source_fingerprint}"
obj_root="$output_root/obj-generated/$profile_tag"
image_root="$output_root/generated-operators/$profile_tag"
mkdir -p "$output_root/obj-generated" "$output_root/generated-operators"
exec {lock_fd}>"$output_root/obj-generated/.lock-$profile_tag"
flock "$lock_fd"

if [[ ! -d $image_root ]]; then
  staging=$(mktemp -d \
    "$output_root/generated-operators/.staging-${profile_tag}.XXXXXXXX")
  cleanup() {
    if [[ -n ${staging:-} && -d $staging ]]; then
      find "$staging" -mindepth 1 -delete
      rmdir "$staging"
    fi
  }
  trap cleanup EXIT
  for source in "${case_sources[@]}"; do
    case_id=$(basename "$source" .c)
    case_kind=$(sed -n -E \
      's/^[[:space:]]*(HPU_CASE_[A-Z0-9_]+),[[:space:]]*$/\1/p' \
      "$repo_root/$source")
    if [[ ! $case_kind =~ ^HPU_CASE_GENERATED_[A-Z0-9_]+$ ]]; then
      printf 'ERROR: invalid generated operator kind in %s\n' "$source" >&2
      exit 2
    fi
    printf '[nexus-am generated] %s\n' "$case_id"
    make -C "$repo_root" -f nexus-am.mk -j"$jobs" \
      ARCH=riscv64-xs LINUX_GNU_TOOLCHAIN=1 \
      CASE="$source" CASE_ID="$case_id" CASE_KIND="$case_kind" \
      HPU_IT_MEM_BASE="$hpu_mem_base" HPU_IT_MEM_LINES="$hpu_mem_lines" \
      HPU_IT_SOURCE_FINGERPRINT="$source_fingerprint" \
      DST_DIR="$obj_root/$case_id/" BINARY="$staging/$case_id"
    operator=${case_id#HPU_IT_GEN_}
    operator=${operator%_001}
    operator=${operator,,}
    cp "$resolved_relocation_root/$operator.csv" "$staging/$case_id.dma.csv"
    (cd "$staging" &&
      riscv64-linux-gnu-objdump -d "$case_id.elf" > "$case_id.txt")
  done
  final_am_status=$(git -C "$AM_HOME" status --porcelain=v1 \
    --untracked-files=all -- . ":(exclude)tests/hpu-it" 2>/dev/null) || exit 2
  final_am_revision=$(git -C "$AM_HOME" rev-parse --verify HEAD)
  final_compiler_identity=$(riscv64-linux-gnu-gcc --version | sed -n '1p')
  final_source_fingerprint=$(hpu_compute_source_fingerprint \
    "$repo_root" "$hpu_mem_base" "$hpu_mem_lines" \
    "$final_am_revision" "$final_compiler_identity" \
    "${fingerprint_inputs[@]}")
  if [[ -n $final_am_status ||
        $final_am_revision != "$am_revision" ||
        $final_compiler_identity != "$compiler_identity" ||
        $final_source_fingerprint != "$source_fingerprint" ]]; then
    printf 'ERROR: source, Nexus-AM, or toolchain identity changed during generated-operator build; refusing publication\n' >&2
    exit 2
  fi
  printf 'HPU_MEM_BASE=%s;HPU_MEM_LINES=%s;HPU_LINE_BYTES=256;HPU_MEM_BYTES=%s\n' \
    "$hpu_mem_base" "$hpu_mem_lines" "$((hpu_mem_lines * 256))" \
    > "$staging/.hpu-mem-profile"
  printf '%s\n' "$source_fingerprint" > "$staging/.source-fingerprint"
  printf '%s\n' "$am_revision" > "$staging/.am-revision"
  printf '%s\n' "$compiler_identity" > "$staging/.toolchain-identity"
  {
    printf 'case_id,delivery_status,reason\n'
    for source in "${case_sources[@]}"; do
      printf '%s,EXECUTABLE_SELF_CHECK_READY,RTL_evidence_required\n' \
        "$(basename "$source" .c)"
    done
  } > "$staging/.qualification-status.csv"
  HPU_IT_EXPECTED_SOURCE_FINGERPRINT="$source_fingerprint" \
  HPU_IT_VALIDATE_STAGING=1 \
    "$script_dir/validate_generated_operators.sh" "$staging"
  mv -T "$staging" "$image_root"
  staging=
  trap - EXIT
else
  "$script_dir/validate_generated_operators.sh" "$image_root"
fi

latest="$output_root/generated-operators/latest"
latest_tmp="$output_root/generated-operators/.latest-$$"
ln -s "$profile_tag" "$latest_tmp"
mv -Tf "$latest_tmp" "$latest"
printf '[nexus-am generated] published 4 ELF/bin/txt sets in %s\n' "$image_root"
