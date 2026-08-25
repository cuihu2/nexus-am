#!/usr/bin/env bash
set -Eeuo pipefail

script_dir=$(cd "$(dirname "$0")" && pwd)
test_root=$(cd "$script_dir/.." && pwd)
output_root=${1:-"$test_root/build"}
case_filter=${2:-}
case_group=${HPU_CASE_GROUP:-all}
jobs=${JOBS:-4}
arch=${ARCH:-riscv64-xs}
cross_compile=${CROSS_COMPILE:-riscv64-linux-gnu-}
roster="$test_root/cases.tsv"

if [[ $output_root != /* ]]; then
  output_root="$PWD/$output_root"
fi
if [[ -z ${AM_HOME:-} || ! -f $AM_HOME/Makefile.app ]]; then
  printf 'ERROR: AM_HOME must point to this Nexus-AM checkout\n' >&2
  exit 2
fi
if [[ ! $jobs =~ ^[1-9][0-9]*$ ]]; then
  printf 'ERROR: JOBS must be a positive integer: %s\n' "$jobs" >&2
  exit 2
fi
if [[ ! $case_group =~ ^(all|core|transform|fhe)$ ]]; then
  printf 'ERROR: HPU_CASE_GROUP must be all, core, transform, or fhe: %s\n' \
    "$case_group" >&2
  exit 2
fi
if [[ -n $case_filter && $case_group != all ]]; then
  printf 'ERROR: CASE filter and HPU_CASE_GROUP cannot be used together\n' >&2
  exit 2
fi

for tool in gcc objcopy objdump readelf strip; do
  command -v "${cross_compile}${tool}" >/dev/null || {
    printf 'ERROR: missing %s%s\n' "$cross_compile" "$tool" >&2
    exit 2
  }
done

if [[ ! -s $roster ]] || \
   [[ $(sed -n '1p' "$roster") != $'group\tqualifier\tcase_id\tsource' ]]; then
  printf 'ERROR: canonical testcase roster is missing or malformed: %s\n' \
    "$roster" >&2
  exit 2
fi

declare -A roster_group=()
declare -A roster_qualifier=()
declare -A roster_source=()
declare -A roster_path_ids=()
roster_ids=()
declare -A roster_group_counts=([core]=0 [transform]=0 [fhe]=0)
roster_migrated=0
roster_migrated_software=0
roster_migrated_blocked=0
while IFS=$'\t' read -r group qualifier case_id source_path; do
  [[ $group == group ]] && continue
  if [[ ! $group =~ ^(core|transform|fhe)$ ]] || \
     [[ ! $qualifier =~ ^(software-self-check|blocked-not-issued|waveform-hold|termination-probe-pass|termination-probe-fail)$ ]] || \
     [[ ! $case_id =~ ^[A-Za-z0-9_]+$ ]] || \
     [[ ! $source_path =~ ^src/(00_bringup|01_configuration|02_data_paths|03_compute_instructions|04_composite_instruction_sequences|05_cpu_hpu_structural_connectivity|06_performance|07_full_application)/[^/]+/[^/]+\.c$ ]] || \
     [[ -n ${roster_group[$case_id]:-} ]] || \
     [[ -n ${roster_path_ids[$source_path]:-} ]]; then
    printf 'ERROR: malformed or duplicate canonical roster row for %s\n' \
      "$case_id" >&2
    exit 2
  fi
  if [[ ! -f $test_root/$source_path ]] || \
     [[ $(basename "${source_path%.c}") != "$case_id" ]]; then
    printf 'ERROR: canonical testcase source is missing or renamed: %s\n' \
      "$source_path" >&2
    exit 2
  fi
  roster_ids+=("$case_id")
  roster_group[$case_id]=$group
  roster_qualifier[$case_id]=$qualifier
  roster_source[$case_id]=$source_path
  roster_path_ids[$source_path]=$case_id
  roster_group_counts[$group]=$((roster_group_counts[$group] + 1))
  if [[ $source_path != src/00_bringup/* ]]; then
    roster_migrated=$((roster_migrated + 1))
    if [[ $qualifier == software-self-check ]]; then
      roster_migrated_software=$((roster_migrated_software + 1))
    elif [[ $qualifier == blocked-not-issued ]]; then
      roster_migrated_blocked=$((roster_migrated_blocked + 1))
    fi
  fi
done < "$roster"

if [[ ${#roster_ids[@]} -ne 58 || ${roster_group_counts[core]} -ne 37 || \
      ${roster_group_counts[transform]} -ne 8 || \
      ${roster_group_counts[fhe]} -ne 13 || $roster_migrated -ne 49 || \
      $roster_migrated_software -ne 25 || $roster_migrated_blocked -ne 24 ]]; then
  printf 'ERROR: canonical testcase roster counts changed unexpectedly\n' >&2
  exit 2
fi

mapfile -t discovered_sources < <(
  find "$test_root/src" -mindepth 3 -maxdepth 3 \
    -type f -name '*.c' ! -path "$test_root/src/common/*" -print | sort
)
if [[ ${#discovered_sources[@]} -ne ${#roster_ids[@]} ]]; then
  printf 'ERROR: source tree contains %u cases; canonical roster requires %u\n' \
    "${#discovered_sources[@]}" "${#roster_ids[@]}" >&2
  exit 2
fi
for source in "${discovered_sources[@]}"; do
  relative_source=src/${source#"$test_root/src/"}
  if [[ -z ${roster_path_ids[$relative_source]:-} ]]; then
    printf 'ERROR: testcase source is not in canonical roster: %s\n' \
      "$relative_source" >&2
    exit 2
  fi
done

if [[ -n $case_filter ]]; then
  case_filter=${case_filter#src/}
  case_filter=${case_filter%.c}
  if [[ $case_filter == /* || $case_filter == *'..'* || \
        ! $case_filter =~ ^(00_bringup|01_configuration|02_data_paths|03_compute_instructions|04_composite_instruction_sequences|05_cpu_hpu_structural_connectivity|06_performance|07_full_application)/[^/]+/[^/]+$ ]]; then
    printf 'ERROR: invalid testcase path: %s\n' "$case_filter" >&2
    exit 2
  fi
  selected_path="src/$case_filter.c"
  selected_id=${roster_path_ids[$selected_path]:-}
  if [[ -z $selected_id ]]; then
    printf 'ERROR: testcase is not in canonical roster: %s\n' \
      "$selected_path" >&2
    exit 2
  fi
  case_sources=("$test_root/$selected_path")
else
  case_sources=()
  for source_case_id in "${roster_ids[@]}"; do
    source_group=${roster_group[$source_case_id]}
    if [[ $case_group == all || $source_group == "$case_group" ]]; then
      case_sources+=("$test_root/${roster_source[$source_case_id]}")
    fi
  done
fi
if [[ ${#case_sources[@]} -eq 0 ]]; then
  printf 'ERROR: no HPU testcase sources found\n' >&2
  exit 2
fi

for source in "${case_sources[@]}"; do
  if [[ ! -f $source ]]; then
    printf 'ERROR: testcase not found: %s\n' "$source" >&2
    exit 2
  fi
done

artifact_root="$output_root/artifact"
object_root="$output_root/obj"
mkdir -p "$artifact_root" "$object_root"
# An artifact directory is one build invocation's publish set.  Keeping ELF
# files from an earlier full/filtered build makes case_count validation lie.
find "$artifact_root" -mindepth 1 -delete

case_manifest="$artifact_root/CASE_MANIFEST.tsv"
not_qualified_manifest="$artifact_root/NOT_QUALIFIED.tsv"
printf 'group\tqualifier\tcase_id\tsource\n' > "$case_manifest"
printf 'case_id\tsource\treason\n' > "$not_qualified_manifest"
declare -A seen_case_ids=()
declare -A group_counts=([core]=0 [transform]=0 [fhe]=0)
not_qualified_count=0

for source in "${case_sources[@]}"; do
  relative=${source#"$test_root/src/"}
  relative_no_ext=${relative%.c}
  case_id=$(basename "$relative_no_ext")
  group=${roster_group[$case_id]}
  qualification=${roster_qualifier[$case_id]}
  if [[ -n ${seen_case_ids[$case_id]:-} ]]; then
    printf 'ERROR: duplicate testcase ID %s: %s and %s\n' \
      "$case_id" "${seen_case_ids[$case_id]}" "$relative" >&2
    exit 2
  fi
  seen_case_ids[$case_id]=$relative
  group_counts[$group]=$((group_counts[$group] + 1))
  printf '%s\t%s\t%s\tsrc/%s\n' \
    "$group" "$qualification" "$case_id" "$relative" >> "$case_manifest"
  if [[ $qualification == blocked-not-issued ]]; then
    printf '%s\tsrc/%s\tno validated inline-asm algorithm binding; HPU commands are not issued and main returns 1\n' \
      "$case_id" "$relative" >> "$not_qualified_manifest"
    not_qualified_count=$((not_qualified_count + 1))
  fi

  binary="$artifact_root/$group/$relative_no_ext"
  object_dir="$object_root/$group/$relative_no_ext"
  mkdir -p "$(dirname "$binary")" "$object_dir"

  printf '[hputest][%s] build %s\n' "$group" "$relative_no_ext"
  make -C "$test_root" -f Makefile.case -j"$jobs" \
    ARCH="$arch" \
    CROSS_COMPILE="$cross_compile" \
    LINUX_GNU_TOOLCHAIN=1 \
    CASE_SOURCE="$source" \
    CASE_ID="$case_id" \
    HPU_DST_DIR="$object_dir/" \
    BINARY="$binary"

  "${cross_compile}strip" --strip-debug "$binary.elf"
  "${cross_compile}objcopy" -O binary "$binary.elf" "$binary.bin"
  (
    cd "$(dirname "$binary")"
    "${cross_compile}objdump" -d "$(basename "$binary").elf" \
      > "$(basename "$binary").txt.tmp"
    mv -f "$(basename "$binary").txt.tmp" "$(basename "$binary").txt"
  )
done

generated_root=${HPU_GENERATED_ROOT:-"$output_root/generated"}
mm_delivery_source="$generated_root/inline-asm/mm"
mm_delivery_artifact="$artifact_root/provenance/inline-asm-mm"
if [[ ! -s $mm_delivery_source/SHA256SUMS ]] || \
   [[ ! -s $mm_delivery_source/PRODUCER_COMMIT ]]; then
  printf 'ERROR: validated inline-asm MM delivery is missing; run prepare-inline-asm-mm\n' >&2
  exit 2
fi
if [[ -d $mm_delivery_artifact ]]; then
  find "$mm_delivery_artifact" -mindepth 1 -delete
else
  mkdir -p "$mm_delivery_artifact"
fi
cp -a "$mm_delivery_source/." "$mm_delivery_artifact/"

(
  cd "$artifact_root"
  find . -type f \( -name '*.elf' -o -name '*.bin' -o -name '*.txt' \) \
    -print0 | sort -z | xargs -0 sha256sum > SHA256SUMS
)
if [[ -n $case_filter ]]; then
  selection="case:$case_filter"
  expected_cases=1
else
  selection=$case_group
  case "$case_group" in
    all) expected_cases=58 ;;
    core) expected_cases=37 ;;
    transform) expected_cases=8 ;;
    fhe) expected_cases=13 ;;
  esac
fi
if [[ ${#case_sources[@]} -ne $expected_cases ]]; then
  printf 'ERROR: selection %s resolved to %u cases; canonical roster requires %u\n' \
    "$selection" "${#case_sources[@]}" "$expected_cases" >&2
  exit 2
fi
{
  printf 'repository=%s\n' "${GITHUB_REPOSITORY:-local}"
  printf 'revision=%s\n' "$(git -C "$AM_HOME" rev-parse HEAD 2>/dev/null || printf unknown)"
  printf 'arch=%s\n' "$arch"
  printf 'toolchain=%s\n' "$("${cross_compile}gcc" --version | sed -n '1p')"
  printf 'selection=%s\n' "$selection"
  printf 'case_count=%u\n' "${#case_sources[@]}"
  printf 'core_count=%u\n' "${group_counts[core]}"
  printf 'transform_count=%u\n' "${group_counts[transform]}"
  printf 'fhe_count=%u\n' "${group_counts[fhe]}"
  printf 'not_qualified_count=%u\n' "$not_qualified_count"
  printf 'inline_asm_commit=%s\n' "$(<"$mm_delivery_source/PRODUCER_COMMIT")"
} > "$artifact_root/MANIFEST.txt"

EXPECTED_CASES=$expected_cases CROSS_COMPILE="$cross_compile" \
  "$script_dir/validate-build.sh" "$artifact_root"
printf '[hputest] PASS: %u testcase artifact sets in %s\n' \
  "${#case_sources[@]}" "$artifact_root"
