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
output_root=${1:-"$repo_root/build"}
jobs=${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 1)}
hpu_mem_base=${HPU_IT_MEM_BASE:-0x87000000}
hpu_mem_lines=${HPU_IT_MEM_LINES:-19201}

# Recursive Nexus-AM make invocations change their working directory.  Resolve
# a caller-provided relative output path once here so object and image targets
# continue to refer to the same publication root at every recursion depth.
if [[ $output_root != /* ]]; then
  output_root="$PWD/$output_root"
fi

if [[ -z ${AM_HOME:-} || ! -f ${AM_HOME}/Makefile.app ]]; then
  printf 'ERROR: AM_HOME must point to OpenXiangShan/nexus-am\n' >&2
  exit 2
fi
# These variables are consumed by Nexus-AM/Make recursively but are not part
# of the artifact fingerprint.  Reject ambient overrides so the same profile
# tag cannot describe binaries built with different flags or tools.
for build_env in CFLAGS CXXFLAGS CPPFLAGS LDFLAGS ASFLAGS MAKEFLAGS MFLAGS \
                 CC CXX LD AR OBJCOPY OBJDUMP CROSS_COMPILE; do
  if [[ -v $build_env ]]; then
    printf 'ERROR: unset ambient build variable %s before publication\n' \
      "$build_env" >&2
    exit 2
  fi
done
if [[ ! $jobs =~ ^[1-9][0-9]*$ ]]; then
  printf 'ERROR: JOBS must be a positive integer: %s\n' "$jobs" >&2
  exit 2
fi
if [[ ! $hpu_mem_base =~ ^(0[xX][0-9a-fA-F]+|[0-9]+)$ ]]; then
  printf 'ERROR: HPU_IT_MEM_BASE must be a decimal or hexadecimal integer: %s\n' \
    "$hpu_mem_base" >&2
  exit 2
fi
if [[ ! $hpu_mem_lines =~ ^[1-9][0-9]*$ ]]; then
  printf 'ERROR: HPU_IT_MEM_LINES must be a positive integer: %s\n' \
    "$hpu_mem_lines" >&2
  exit 2
fi
hpu_mem_lines=$((10#$hpu_mem_lines))
if (( hpu_mem_lines < 19201 )); then
  printf 'ERROR: HPU_IT_MEM_LINES must be at least 19201 for FHE scratch and guard\n' >&2
  exit 2
fi
if (( hpu_mem_lines > 36028797018963967 )); then
  printf 'ERROR: HPU_IT_MEM_LINES overflows byte-size calculation: %s\n' \
    "$hpu_mem_lines" >&2
  exit 2
fi

for tool in gcc ld ar objcopy objdump readelf strings; do
  command -v "riscv64-linux-gnu-$tool" >/dev/null || {
    printf 'ERROR: missing riscv64-linux-gnu-%s\n' "$tool" >&2
    exit 2
  }
done
for tool in sha256sum cmp flock git; do
  command -v "$tool" >/dev/null || {
    printf 'ERROR: missing %s\n' "$tool" >&2
    exit 2
  }
done

am_status=$(git -C "$AM_HOME" status --porcelain=v1 \
  --untracked-files=all -- . ":(exclude)tests/hpu-it" 2>/dev/null) || {
  printf 'ERROR: AM_HOME is not a readable Git worktree: %s\n' \
    "$AM_HOME" >&2
  exit 2
}
if [[ -n $am_status ]]; then
  printf 'ERROR: Nexus-AM worktree must be clean for a reproducible publication: %s\n' \
    "$AM_HOME" >&2
  printf '%s\n' "$am_status" >&2
  exit 2
fi

mkdir -p "$output_root/obj" "$output_root/images"
bash "$script_dir/prepare_inline_vectors.sh"
resolved_relocation_root=$(
  bash "$script_dir/prepare_resolved_relocations.sh" | tail -n 1
)
mapfile -t case_sources < <(hpu_case_sources "$repo_root")
if [[ ${#case_sources[@]} -ne 49 ]]; then
  printf 'ERROR: expected 49 testcase sources, found %d\n' "${#case_sources[@]}" >&2
  exit 2
fi

mapfile -t fingerprint_inputs < <(hpu_fingerprint_inputs "$repo_root")
compiler_identity=$(riscv64-linux-gnu-gcc --version | sed -n '1p')
am_revision=$(git -C "$AM_HOME" rev-parse --verify HEAD 2>/dev/null ||
  printf 'unmanaged:%s' "$AM_HOME")
source_fingerprint=$(hpu_compute_source_fingerprint \
  "$repo_root" "$hpu_mem_base" "$hpu_mem_lines" \
  "$am_revision" "$compiler_identity" "${fingerprint_inputs[@]}")
profile_tag="hpu-${hpu_mem_base}-${hpu_mem_lines}-${source_fingerprint}"
profile_obj_root="$output_root/obj/$profile_tag"
profile_image_root="$output_root/images/$profile_tag"
# Object files are cached per fingerprint, so serialize identical builds while
# still allowing different memory/source profiles to build in parallel.
profile_lock="$output_root/obj/.lock-$profile_tag"
exec {profile_lock_fd}>"$profile_lock"
flock "$profile_lock_fd"

if [[ -e $profile_image_root || -L $profile_image_root ]]; then
  if [[ ! -d $profile_image_root ]]; then
    printf 'ERROR: profile image path exists and is not a directory: %s\n' \
      "$profile_image_root" >&2
    exit 2
  fi
  printf '[nexus-am] validating existing immutable profile %s\n' "$profile_tag"
  HPU_IT_MEM_BASE="$hpu_mem_base" HPU_IT_MEM_LINES="$hpu_mem_lines" \
    "$script_dir/validate_artifacts.sh" "$profile_image_root"
else
  staging_image_root=$(mktemp -d \
    "$output_root/images/.staging-${profile_tag}.XXXXXXXX")
  cleanup_staging() {
    if [[ -n ${staging_image_root:-} && -d $staging_image_root ]]; then
      find "$staging_image_root" -mindepth 1 -delete
      rmdir "$staging_image_root"
    fi
  }
  trap cleanup_staging EXIT
  trap 'exit 130' HUP INT TERM

  for case_source in "${case_sources[@]}"; do
    case_id=$(basename "$case_source" .c)
    case_kind=$(sed -n -E \
      's/^[[:space:]]*(HPU_CASE_[A-Z0-9_]+),[[:space:]]*$/\1/p' \
      "$repo_root/$case_source")
    if [[ ! $case_kind =~ ^HPU_CASE_[A-Z0-9_]+$ ]]; then
      printf 'ERROR: cannot resolve one testcase kind from %s: %s\n' \
        "$case_source" "$case_kind" >&2
      exit 2
    fi
    printf '[nexus-am] %s\n' "$case_id"
    make -C "$repo_root" -f nexus-am.mk -j"$jobs" \
      ARCH=riscv64-xs \
      LINUX_GNU_TOOLCHAIN=1 \
      CASE="$case_source" \
      CASE_ID="$case_id" \
      CASE_KIND="$case_kind" \
      HPU_IT_MEM_BASE="$hpu_mem_base" \
      HPU_IT_MEM_LINES="$hpu_mem_lines" \
      HPU_IT_SOURCE_FINGERPRINT="$source_fingerprint" \
      DST_DIR="$profile_obj_root/$case_id/" \
      BINARY="$staging_image_root/$case_id"
    case "$case_id" in
      HPU_IT_DIR_INS_C0_003)
        cp "$resolved_relocation_root/mm.csv" \
          "$staging_image_root/$case_id.dma.csv" ;;
      HPU_IT_DIR_CMB_005|HPU_IT_DIR_CMB_014)
        cp "$resolved_relocation_root/auto.csv" \
          "$staging_image_root/$case_id.dma.csv" ;;
      HPU_IT_DIR_CMB_011)
        cp "$resolved_relocation_root/encode.csv" \
          "$staging_image_root/$case_id.dma.csv" ;;
      HPU_IT_DIR_CMB_013)
        cp "$resolved_relocation_root/rescale.csv" \
          "$staging_image_root/$case_id.dma.csv" ;;
      HPU_IT_DIR_CMB_004|HPU_IT_DIR_PERF_005)
        cp "$resolved_relocation_root/keyswitch.csv" \
          "$staging_image_root/$case_id.dma.csv" ;;
      HPU_IT_DIR_CMB_012|HPU_IT_DIR_PERF_001)
        cp "$resolved_relocation_root/relinearization.csv" \
          "$staging_image_root/$case_id.dma.csv" ;;
      HPU_IT_DIR_CMB_010|HPU_IT_DIR_PERF_006|HPU_IT_DIR_APP_001)
        cp "$resolved_relocation_root/ciphertext_multiply.csv" \
          "$staging_image_root/$case_id.dma.csv" ;;
    esac
  done

  # Nexus-AM's default objdump header contains the staging path.  Regenerate
  # from inside the image directory so .txt is path-stable after publication
  # and can be compared byte-for-byte by the validator.
  for case_source in "${case_sources[@]}"; do
    case_id=$(basename "$case_source" .c)
    (
      cd "$staging_image_root"
      riscv64-linux-gnu-objdump -d "$case_id.elf" > "$case_id.txt.tmp"
      mv -f "$case_id.txt.tmp" "$case_id.txt"
    )
  done

  # Refuse an atomic publication if source, Nexus-AM, or toolchain identity
  # changed while the 49 executables were being built.  Otherwise one tag
  # could silently contain a mixture of two source snapshots.
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
    printf 'ERROR: source, Nexus-AM, or toolchain identity changed during the build; refusing publication\n' >&2
    exit 2
  fi

  printf 'HPU_MEM_BASE=%s;HPU_MEM_LINES=%s;HPU_LINE_BYTES=256;HPU_MEM_BYTES=%s\n' \
    "$hpu_mem_base" "$hpu_mem_lines" "$((hpu_mem_lines * 256))" \
    > "$staging_image_root/.hpu-mem-profile"
  printf '%s\n' "$source_fingerprint" \
    > "$staging_image_root/.source-fingerprint"
  printf '%s\n' "$am_revision" > "$staging_image_root/.am-revision"
  printf '%s\n' "$compiler_identity" \
    > "$staging_image_root/.toolchain-identity"
  {
    printf 'case_id,delivery_status,reason\n'
    for case_source in "${case_sources[@]}"; do
      case_id=$(basename "$case_source" .c)
      case "$case_id" in
        HPU_IT_DIR_CMB_015)
          printf '%s,BLOCKED_EXTERNAL_ALGORITHM_CONTRACT,HPU_commands_not_issued\n' \
            "$case_id" ;;
        *)
          printf '%s,EXECUTABLE_PASS_READY,output_check_authoritative\n' \
            "$case_id" ;;
      esac
    done
  } > "$staging_image_root/.qualification-status.csv"

  HPU_IT_MEM_BASE="$hpu_mem_base" HPU_IT_MEM_LINES="$hpu_mem_lines" \
    HPU_IT_EXPECTED_SOURCE_FINGERPRINT="$source_fingerprint" \
    HPU_IT_VALIDATE_STAGING=1 \
    "$script_dir/validate_artifacts.sh" "$staging_image_root"

  # The fingerprint directory is immutable once published.  Since staging and
  # final live below the same images directory, this rename is atomic.
  mv -T "$staging_image_root" "$profile_image_root"
  staging_image_root=
  trap - EXIT HUP INT TERM
fi

latest_link="$output_root/images/latest"
if [[ -e $latest_link && ! -L $latest_link ]]; then
  printf 'ERROR: refusing to replace non-symlink latest path: %s\n' \
    "$latest_link" >&2
  exit 2
fi
latest_tmp="$output_root/images/.latest-${profile_tag}-$$"
if [[ -e $latest_tmp || -L $latest_tmp ]]; then
  printf 'ERROR: temporary latest link already exists: %s\n' "$latest_tmp" >&2
  exit 2
fi
ln -s "$profile_tag" "$latest_tmp"
mv -Tf "$latest_tmp" "$latest_link"

printf '[nexus-am] published %d validated ELF images in %s (base=%s lines=%s fingerprint=%s)\n' \
  "${#case_sources[@]}" "$profile_image_root" "$hpu_mem_base" \
  "$hpu_mem_lines" "$source_fingerprint"
