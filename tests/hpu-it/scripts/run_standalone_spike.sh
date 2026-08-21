#!/usr/bin/env bash
set -Eeuo pipefail

# Build testcase ELFs for the non-DIFFTEST CUIHU2 Spike executable and run
# their functional-model self-checks.  This is deliberately separate from
# IT qualification: MMIO/PLIC and external monitor requirements remain open.

script_dir=$(cd "$(dirname "$0")" && pwd)
repo_root=$(cd "$script_dir/.." && pwd)
profile_lib="$script_dir/artifact_profile_lib.sh"
if [[ ! -r $profile_lib ]]; then
  printf 'ERROR: missing artifact profile helper: %s\n' "$profile_lib" >&2
  exit 2
fi
# shellcheck source=artifact_profile_lib.sh
source "$profile_lib"

am_home=${AM_HOME:-/home/l/IT/nexus-am}
spike=${SPIKE:-/home/l/hpu/IT-SCPU-RTL/tools/hpu/nanhu-spike/build-hpu-19201-standalone/spike}
spike_config_log=${SPIKE_CONFIG_LOG:-"$(dirname "$spike")/config.log"}
output_root=${OUTPUT_ROOT:-"$repo_root/build-standalone-spike"}
jobs=${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 1)}
timeout_seconds=${SPIKE_TIMEOUT_SECONDS:-300}
hpu_mem_base=${HPU_IT_MEM_BASE:-0x87000000}
hpu_mem_lines=${HPU_IT_MEM_LINES:-19201}
# Nexus-AM's current riscv64-xs build may emit Zbc bit-manipulation helpers
# (for example during the full Relin path), so keep the standalone model ISA
# aligned with the linked ELF rather than relying on Spike's rv64gc default.
isa=${SPIKE_ISA:-rv64imafdc_zicbom_zba_zbb_zbc_zbs}
standalone_model_contract="fixed_aperture=1 active_window_translation=software csr_mmio=UNMODELED plic=UNMODELED"

if [[ ! -f $am_home/Makefile.app ]]; then
  printf 'ERROR: AM_HOME is not a Nexus-AM checkout: %s\n' "$am_home" >&2
  exit 2
fi
if [[ ! -x $spike ]]; then
  printf 'ERROR: standalone Spike executable is missing: %s\n' "$spike" >&2
  exit 2
fi
if [[ ! -r $spike_config_log ]]; then
  printf 'ERROR: standalone Spike config.log is required: %s\n' \
    "$spike_config_log" >&2
  exit 2
fi
for build_env in CFLAGS CXXFLAGS CPPFLAGS LDFLAGS ASFLAGS MAKEFLAGS MFLAGS \
                 CC CXX LD AR OBJCOPY OBJDUMP CROSS_COMPILE; do
  if [[ -v $build_env ]]; then
    printf 'ERROR: unset ambient build variable %s before standalone execution\n' \
      "$build_env" >&2
    exit 2
  fi
done
if [[ ! $jobs =~ ^[1-9][0-9]*$ ||
      ! $timeout_seconds =~ ^[1-9][0-9]*$ ||
      ! $hpu_mem_lines =~ ^[1-9][0-9]*$ ||
      ! $hpu_mem_base =~ ^(0[xX][0-9a-fA-F]+|[0-9]+)$ ]]; then
  printf 'ERROR: invalid JOBS, timeout, or HPU memory profile\n' >&2
  exit 2
fi
hpu_mem_lines=$((10#$hpu_mem_lines))
if (( hpu_mem_lines < 19201 )); then
  printf 'ERROR: HPU_IT_MEM_LINES must be at least 19201\n' >&2
  exit 2
fi

for tool in basename cut find flock git make mkdir mv rmdir sort timeout \
            sha256sum grep sed wc \
            riscv64-linux-gnu-gcc \
            riscv64-linux-gnu-readelf riscv64-linux-gnu-nm; do
  command -v "$tool" >/dev/null || {
    printf 'ERROR: missing required tool: %s\n' "$tool" >&2
    exit 2
  }
done

am_status=$(git -C "$am_home" status --porcelain=v1 \
  --untracked-files=all 2>/dev/null) || {
  printf 'ERROR: AM_HOME is not a readable Git worktree: %s\n' \
    "$am_home" >&2
  exit 2
}
if [[ -n $am_status ]]; then
  printf 'ERROR: Nexus-AM worktree must be clean for reproducible standalone artifacts: %s\n' \
    "$am_home" >&2
  printf '%s\n' "$am_status" >&2
  exit 2
fi

# A DIFFTEST-mode Spike is a shared-library backend and does not load the ELF
# supplied on its CLI.  Reject it explicitly; otherwise a timeout at PC 0 is
# easily misdiagnosed as an HPU algorithm failure.
for required_flag in -DCPU_NANHU -DENABLE_HPU \
                     "-DCONFIG_HPU_MEM_BASE=$hpu_mem_base" \
                     "-DCONFIG_HPU_MEM_LINES=$hpu_mem_lines"; do
  grep -Fq -- "$required_flag" "$spike_config_log" || {
    printf 'ERROR: Spike config lacks %s: %s\n' \
      "$required_flag" "$spike_config_log" >&2
    exit 2
  }
done
if grep -Eq -- "-D(DIFFTEST|CONFIG_DIFFTEST)(=1)?([[:space:]\"';]|$)" \
    "$spike_config_log"; then
  printf 'ERROR: refusing a DIFFTEST-mode Spike for standalone ELF runs: %s\n' \
    "$spike" >&2
  exit 2
fi

mapfile -t all_sources < <(hpu_case_sources "$repo_root")
if [[ ${#all_sources[@]} -ne 49 ]]; then
  printf 'ERROR: expected 49 testcase sources, found %d\n' \
    "${#all_sources[@]}" >&2
  exit 2
fi

declare -A source_by_id=()
for source in "${all_sources[@]}"; do
  case_id=$(basename "$source" .c)
  source_by_id["$case_id"]=$source
done

case_ids=()
if (( $# == 0 )); then
  for source in "${all_sources[@]}"; do
    case_ids+=("$(basename "$source" .c)")
  done
else
  declare -A selected=()
  for case_id in "$@"; do
    if [[ -z ${source_by_id[$case_id]+present} ]]; then
      printf 'ERROR: unknown testcase ID: %s\n' "$case_id" >&2
      exit 2
    fi
    if [[ -n ${selected[$case_id]+present} ]]; then
      printf 'ERROR: duplicate testcase ID: %s\n' "$case_id" >&2
      exit 2
    fi
    selected["$case_id"]=1
    case_ids+=("$case_id")
  done
fi

selection_sha=$(printf '%s\n' "${case_ids[@]}" | sha256sum | cut -c1-16)

bash "$script_dir/prepare_inline_vectors.sh"
mapfile -t fingerprint_inputs < <(hpu_fingerprint_inputs "$repo_root")
compiler_identity=$(riscv64-linux-gnu-gcc --version | sed -n '1p')
am_revision=$(git -C "$am_home" rev-parse --verify HEAD 2>/dev/null ||
  printf 'unmanaged:%s' "$am_home")
source_fingerprint=$(hpu_compute_source_fingerprint \
  "$repo_root" "$hpu_mem_base" "$hpu_mem_lines" \
  "$am_revision" "$compiler_identity" "${fingerprint_inputs[@]}")
spike_sha=$(sha256sum "$spike" | cut -d' ' -f1)
spike_config_sha=$(sha256sum "$spike_config_log" | cut -d' ' -f1)
standalone_script_sha=$(sha256sum "$script_dir/run_standalone_spike.sh" | \
  cut -d' ' -f1)
run_fingerprint=$(printf '%s\n' \
  "$source_fingerprint" "$spike_sha" "$spike_config_sha" \
  "$standalone_script_sha" "$isa" "$selection_sha" | \
  sha256sum | cut -c1-16)
profile_tag="hpu-${hpu_mem_base}-${hpu_mem_lines}-${source_fingerprint}-standalone-${run_fingerprint}"
profile_root="$output_root/$profile_tag"
mkdir -p "$output_root/.locks"
output_root=$(cd "$output_root" && pwd -P)
profile_root="$output_root/$profile_tag"
lock_file="$output_root/.locks/$profile_tag.lock"
exec 9>"$lock_file"
flock 9
if [[ -e $profile_root || -L $profile_root ]]; then
  printf 'ERROR: refusing to reuse an existing standalone run directory: %s\n' \
    "$profile_root" >&2
  printf '       choose a fresh OUTPUT_ROOT to prove a new execution\n' >&2
  exit 2
fi
staging_root="$output_root/.staging-$profile_tag.$BASHPID"
if [[ -e $staging_root || -L $staging_root ]]; then
  printf 'ERROR: standalone staging path already exists: %s\n' \
    "$staging_root" >&2
  exit 2
fi
mkdir -p "$staging_root/obj" "$staging_root/images" \
  "$staging_root/logs"
cleanup_standalone_staging() {
  if [[ -d $staging_root ]]; then
    find "$staging_root" -mindepth 1 -delete
    rmdir "$staging_root"
  fi
}
trap cleanup_standalone_staging EXIT

printf 'HPU_MEM_BASE=%s;HPU_MEM_LINES=%s;HPU_LINE_BYTES=256;HPU_MEM_BYTES=%s\n' \
  "$hpu_mem_base" "$hpu_mem_lines" "$((hpu_mem_lines * 256))" \
  >"$staging_root/.hpu-mem-profile"
printf '%s\n' "$source_fingerprint" >"$staging_root/.source-fingerprint"
printf '%s\n' "$am_revision" >"$staging_root/.am-revision"
printf '%s\n' "$compiler_identity" \
  >"$staging_root/.toolchain-identity"
printf '%s\n' "$spike" >"$staging_root/.standalone-spike"
printf '%s\n' "$spike_sha" >"$staging_root/.standalone-spike-sha256"
printf '%s\n' "$spike_config_log" >"$staging_root/.standalone-config-log"
printf '%s\n' "$spike_config_sha" \
  >"$staging_root/.standalone-config-sha256"
printf '%s\n' "$standalone_script_sha" \
  >"$staging_root/.standalone-script-sha256"
printf '%s\n' "$isa" >"$staging_root/.standalone-isa"
printf '%s\n' "${case_ids[@]}" >"$staging_root/.case-manifest"
printf '%s\n' "$selection_sha" >"$staging_root/.case-selection-sha256"
printf '%s\n' "$run_fingerprint" \
  >"$staging_root/.standalone-run-fingerprint"
printf '%s\n' "$standalone_model_contract" \
  >"$staging_root/.standalone-model-contract"

printf '[standalone-spike] functional model only; IT qualification remains PASS_PROBE\n'
printf '[standalone-spike] spike=%s profile=%s cases=%d\n' \
  "$spike" "$profile_tag" "${#case_ids[@]}"

passed=0
for case_id in "${case_ids[@]}"; do
  source=${source_by_id[$case_id]}
  binary="$staging_root/images/$case_id"
  log="$staging_root/logs/$case_id.log"
  printf '[standalone-spike] build %s\n' "$case_id"
  make -C "$repo_root" -f nexus-am.mk -j"$jobs" \
    AM_HOME="$am_home" \
    ARCH=riscv64-xs \
    LINUX_GNU_TOOLCHAIN=1 \
    CASE="$source" \
    CASE_ID="$case_id" \
    HPU_IT_MEM_BASE="$hpu_mem_base" \
    HPU_IT_MEM_LINES="$hpu_mem_lines" \
    HPU_IT_SOURCE_FINGERPRINT="$source_fingerprint" \
    HPU_IT_ENABLE_MMIO=0 \
    HPU_IT_WAIT_IRQ=0 \
    HPU_IT_STANDALONE_HTIF=1 \
    DST_DIR="$staging_root/obj/$case_id/" \
    BINARY="$binary"

  elf="$binary.elf"
  header=$(riscv64-linux-gnu-readelf -h "$elf")
  grep -Fq 'Machine:                           RISC-V' <<<"$header" &&
    grep -Fq 'Entry point address:               0x80000000' <<<"$header" || {
      printf 'ERROR: malformed standalone ELF: %s\n' "$elf" >&2
      exit 2
    }
  nm_output=$(riscv64-linux-gnu-nm "$elf")
  grep -Eq '[[:space:]]tohost$' <<<"$nm_output" || {
    printf 'ERROR: standalone ELF lacks tohost: %s\n' "$elf" >&2
    exit 2
  }
  printf '[standalone-spike] run   %s\n' "$case_id"
  printf 'HPU_IT_STANDALONE_MODEL %s\n' "$standalone_model_contract" \
    >"$log"
  set +e
  timeout --foreground "$timeout_seconds" \
    "$spike" --isa="$isa" "$elf" >>"$log" 2>&1
  status=$?
  set -e
  if (( status != 0 )); then
    printf 'ERROR: %s failed in standalone Spike (status=%d, log=%s)\n' \
      "$case_id" "$status" "$log" >&2
    exit "$status"
  fi
  passed=$((passed + 1))
done

expected_count=${#case_ids[@]}
if (( passed != expected_count )); then
  printf 'ERROR: standalone run count mismatch: passed=%d expected=%d\n' \
    "$passed" "$expected_count" >&2
  exit 2
fi

# A full run can take long enough for an accidental editor/build to replace
# one of its inputs.  Recompute every identity before publication so a mixed
# execution can never acquire the initial profile tag and .complete marker.
final_am_status=$(git -C "$am_home" status --porcelain=v1 \
  --untracked-files=all 2>/dev/null) || exit 2
final_am_revision=$(git -C "$am_home" rev-parse --verify HEAD)
final_compiler_identity=$(riscv64-linux-gnu-gcc --version | sed -n '1p')
final_source_fingerprint=$(hpu_compute_source_fingerprint \
  "$repo_root" "$hpu_mem_base" "$hpu_mem_lines" \
  "$final_am_revision" "$final_compiler_identity" \
  "${fingerprint_inputs[@]}")
final_spike_sha=$(sha256sum "$spike" | cut -d' ' -f1)
final_spike_config_sha=$(sha256sum "$spike_config_log" | cut -d' ' -f1)
final_standalone_script_sha=$( \
  sha256sum "$script_dir/run_standalone_spike.sh" | cut -d' ' -f1)
final_run_fingerprint=$(printf '%s\n' \
  "$final_source_fingerprint" "$final_spike_sha" \
  "$final_spike_config_sha" "$final_standalone_script_sha" \
  "$isa" "$selection_sha" | sha256sum | cut -c1-16)
if [[ -n $final_am_status ||
      $final_am_revision != "$am_revision" ||
      $final_compiler_identity != "$compiler_identity" ||
      $final_source_fingerprint != "$source_fingerprint" ||
      $final_spike_sha != "$spike_sha" ||
      $final_spike_config_sha != "$spike_config_sha" ||
      $final_standalone_script_sha != "$standalone_script_sha" ||
      $final_run_fingerprint != "$run_fingerprint" ]]; then
  printf 'ERROR: standalone source/tool/model identity changed during the run; refusing publication\n' >&2
  exit 2
fi

for suffix in elf bin txt; do
  count=$(find "$staging_root/images" -maxdepth 1 -type f \
    -name "HPU_IT_*.$suffix" | wc -l)
  if (( count != expected_count )); then
    printf 'ERROR: expected %d standalone .%s files, found %d\n' \
      "$expected_count" "$suffix" "$count" >&2
    exit 2
  fi
done
log_count=$(find "$staging_root/logs" -maxdepth 1 -type f \
  -name 'HPU_IT_*.log' | wc -l)
if (( log_count != expected_count )); then
  printf 'ERROR: expected %d standalone logs, found %d\n' \
    "$expected_count" "$log_count" >&2
  exit 2
fi

complete_line="PASS: $passed standalone HPU functional-model self-checks; not IT qualification"
printf '%s\n' "$complete_line" >"$staging_root/.complete"
mv "$staging_root" "$profile_root"
trap - EXIT
printf '%s (%s)\n' "$complete_line" "$profile_root"
