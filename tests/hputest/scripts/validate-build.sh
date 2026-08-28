#!/usr/bin/env bash
set -Eeuo pipefail

artifact_root=${1:-}
cross_compile=${CROSS_COMPILE:-riscv64-linux-gnu-}
script_dir=$(cd "$(dirname "$0")" && pwd)
test_root=$(cd "$script_dir/.." && pwd)
roster="$test_root/cases.tsv"

if [[ -z $artifact_root || ! -d $artifact_root ]]; then
  printf 'ERROR: artifact directory is required\n' >&2
  exit 2
fi
artifact_root=$(cd "$artifact_root" && pwd)

for tool in gcc nm objcopy objdump readelf; do
  command -v "${cross_compile}${tool}" >/dev/null || {
    printf 'ERROR: missing %s%s\n' "$cross_compile" "$tool" >&2
    exit 2
  }
done

if [[ ! -s $roster ]] || \
   [[ $(sed -n '1p' "$roster") != $'group\tqualifier\tcase_id\tsource' ]] || \
   ! awk -F '\t' 'NF != 4 { exit 1 }' "$roster"; then
  printf 'ERROR: canonical testcase roster is missing or malformed: %s\n' \
    "$roster" >&2
  exit 2
fi

declare -A roster_group=()
declare -A roster_qualifier=()
declare -A roster_source=()
declare -A roster_path_ids=()
declare -A roster_group_counts=([core]=0 [transform]=0 [fhe]=0)
declare -A roster_qualifier_counts=(
  [software-self-check]=0
  [blocked-not-issued]=0
  [waveform-hold]=0
  [termination-probe-pass]=0
  [termination-probe-fail]=0
)
roster_ids=()
roster_migrated=0
roster_migrated_software=0
roster_migrated_blocked=0
while IFS=$'\t' read -r group qualifier case_id source_path; do
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
  source_file="$test_root/$source_path"
  if [[ ! -f $source_file ]] || \
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
  roster_qualifier_counts[$qualifier]=$((roster_qualifier_counts[$qualifier] + 1))
  if [[ $source_path != src/00_bringup/* ]]; then
    roster_migrated=$((roster_migrated + 1))
    case "$qualifier" in
      software-self-check)
        roster_migrated_software=$((roster_migrated_software + 1)) ;;
      blocked-not-issued)
        roster_migrated_blocked=$((roster_migrated_blocked + 1)) ;;
      *)
        printf 'ERROR: migrated testcase has unsupported qualifier: %s %s\n' \
          "$case_id" "$qualifier" >&2
        exit 2 ;;
    esac
  fi
done < <(tail -n +2 "$roster")

if [[ ${#roster_ids[@]} -ne 58 || ${roster_group_counts[core]} -ne 37 || \
      ${roster_group_counts[transform]} -ne 8 || \
      ${roster_group_counts[fhe]} -ne 13 || $roster_migrated -ne 49 || \
      $roster_migrated_software -ne 25 || $roster_migrated_blocked -ne 24 || \
      ${roster_qualifier_counts[software-self-check]} -ne 31 || \
      ${roster_qualifier_counts[blocked-not-issued]} -ne 24 || \
      ${roster_qualifier_counts[waveform-hold]} -ne 1 || \
      ${roster_qualifier_counts[termination-probe-pass]} -ne 1 || \
      ${roster_qualifier_counts[termination-probe-fail]} -ne 1 ]]; then
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
for source_file in "${discovered_sources[@]}"; do
  source_path=src/${source_file#"$test_root/src/"}
  if [[ -z ${roster_path_ids[$source_path]:-} ]]; then
    printf 'ERROR: testcase source is not in canonical roster: %s\n' \
      "$source_path" >&2
    exit 2
  fi
done

# The source policy is checked for the complete tracked roster even when this
# artifact contains only one build group.  A filtered build must not hide a
# false-PASS source elsewhere in the tree.
for case_id in "${roster_ids[@]}"; do
  source_path=${roster_source[$case_id]}
  [[ $source_path == src/00_bringup/* ]] && continue
  source_file="$test_root/$source_path"
  if ! grep -Fq "#define CASE_ID \"$case_id\"" "$source_file"; then
    printf 'ERROR: migrated testcase CASE_ID does not match canonical roster: %s\n' \
      "$source_path" >&2
    exit 2
  fi
  source_code=$("${cross_compile}gcc" -fpreprocessed -E -P "$source_file")
  qualifier=${roster_qualifier[$case_id]}
  if [[ $qualifier == software-self-check ]] && \
     grep -Eq 'hpu_it_run_[[:alnum:]_]*[[:space:]]*\(' <<< "$source_code"; then
    printf 'ERROR: migrated software testcase hides main() in hpu_it_run_*: %s\n' \
      "$source_path" >&2
    exit 2
  fi
  if [[ $qualifier == blocked-not-issued ]]; then
    return_count=$(grep -Eoc '(^|[^[:alnum:]_])return([^[:alnum:]_]|$)' \
      <<< "$source_code" || true)
    return_one_count=$(grep -Eoc \
      'return[[:space:]]+1[Uu]?[[:space:]]*;' <<< "$source_code" || true)
    if [[ $return_count -ne 1 || $return_one_count -ne 1 ]] || \
       grep -Eq 'return[[:space:]]+0[Uu]?[[:space:]]*;' <<< "$source_code" || \
       grep -Eq '(^|[^[:alnum:]_])(if|else|for|while|do|switch|goto)([^[:alnum:]_]|$)' \
         <<< "$source_code"; then
      printf 'ERROR: blocked testcase is not an unconditional return 1: %s\n' \
        "$source_path" >&2
      exit 2
    fi
  fi
done
manifest="$artifact_root/MANIFEST.txt"
if [[ ! -s $manifest ]]; then
  printf 'ERROR: MANIFEST.txt is missing\n' >&2
  exit 2
fi

manifest_value() {
  local key=$1
  local values=()

  mapfile -t values < <(sed -n "s/^${key}=//p" "$manifest")
  if [[ ${#values[@]} -ne 1 || -z ${values[0]} ]]; then
    printf 'ERROR: MANIFEST.txt must contain exactly one non-empty %s\n' \
      "$key" >&2
    return 2
  fi
  printf '%s\n' "${values[0]}"
}

manifest_cases=$(manifest_value case_count)
manifest_inline_asm=$(manifest_value inline_asm_commit)
manifest_selection=$(manifest_value selection)
manifest_core=$(manifest_value core_count)
manifest_transform=$(manifest_value transform_count)
manifest_fhe=$(manifest_value fhe_count)
manifest_not_qualified=$(manifest_value not_qualified_count)

declare -A selected_ids=()
declare -A selected_group_counts=([core]=0 [transform]=0 [fhe]=0)
selected_case_ids=()
selected_not_qualified=0
case "$manifest_selection" in
  all|core|transform|fhe)
    for case_id in "${roster_ids[@]}"; do
      group=${roster_group[$case_id]}
      if [[ $manifest_selection != all && $group != "$manifest_selection" ]]; then
        continue
      fi
      selected_case_ids+=("$case_id")
      selected_ids[$case_id]=1
      selected_group_counts[$group]=$((selected_group_counts[$group] + 1))
      if [[ ${roster_qualifier[$case_id]} == blocked-not-issued ]]; then
        selected_not_qualified=$((selected_not_qualified + 1))
      fi
    done
    ;;
  case:*)
    selected_relative=${manifest_selection#case:}
    if [[ ! $selected_relative =~ ^(00_bringup|01_configuration|02_data_paths|03_compute_instructions|04_composite_instruction_sequences|05_cpu_hpu_structural_connectivity|06_performance|07_full_application)/[^/]+/[^/]+$ ]]; then
      printf 'ERROR: malformed testcase selection: %s\n' \
        "$manifest_selection" >&2
      exit 2
    fi
    selected_source="src/$selected_relative.c"
    selected_case_id=${roster_path_ids[$selected_source]:-}
    if [[ -z $selected_case_id ]]; then
      printf 'ERROR: testcase selection is not in canonical roster: %s\n' \
        "$selected_source" >&2
      exit 2
    fi
    selected_case_ids+=("$selected_case_id")
    selected_ids[$selected_case_id]=1
    group=${roster_group[$selected_case_id]}
    selected_group_counts[$group]=1
    if [[ ${roster_qualifier[$selected_case_id]} == blocked-not-issued ]]; then
      selected_not_qualified=1
    fi
    ;;
  *)
    printf 'ERROR: unsupported MANIFEST.txt selection: %s\n' \
      "$manifest_selection" >&2
    exit 2 ;;
esac
expected_cases=${#selected_case_ids[@]}
if [[ -n ${EXPECTED_CASES:-} ]]; then
  if [[ ! $EXPECTED_CASES =~ ^[1-9][0-9]*$ ]] || \
     [[ $EXPECTED_CASES -ne $expected_cases ]]; then
    printf 'ERROR: EXPECTED_CASES=%s disagrees with canonical selection count=%u\n' \
      "$EXPECTED_CASES" "$expected_cases" >&2
    exit 2
  fi
fi
for count in "$manifest_cases" "$manifest_core" "$manifest_transform" \
             "$manifest_fhe" "$manifest_not_qualified"; do
  if [[ ! $count =~ ^[0-9]+$ ]]; then
    printf 'ERROR: malformed case/group/not-qualified count in MANIFEST.txt\n' >&2
    exit 2
  fi
done
if [[ $manifest_cases -ne $expected_cases || \
      $manifest_core -ne ${selected_group_counts[core]} || \
      $manifest_transform -ne ${selected_group_counts[transform]} || \
      $manifest_fhe -ne ${selected_group_counts[fhe]} || \
      $manifest_not_qualified -ne $selected_not_qualified ]]; then
  printf 'ERROR: MANIFEST.txt counts disagree with canonical selection %s\n' \
    "$manifest_selection" >&2
  exit 2
fi

case_manifest="$artifact_root/CASE_MANIFEST.tsv"
not_qualified_manifest="$artifact_root/NOT_QUALIFIED.tsv"
if [[ ! -s $case_manifest || ! -s $not_qualified_manifest ]] || \
   [[ $(sed -n '1p' "$case_manifest") != $'group\tqualifier\tcase_id\tsource' ]] || \
   [[ $(sed -n '1p' "$not_qualified_manifest") != $'case_id\tsource\treason' ]] || \
   ! awk -F '\t' 'NF != 4 { exit 1 }' "$case_manifest" || \
   ! awk -F '\t' 'NF != 3 { exit 1 }' "$not_qualified_manifest"; then
  printf 'ERROR: testcase qualifier manifests are missing or malformed\n' >&2
  exit 2
fi

declare -A manifest_ids=()
declare -A expected_elf_by_id=()
declare -A actual_group_counts=([core]=0 [transform]=0 [fhe]=0)
actual_cases=0
actual_not_qualified=0
while IFS=$'\t' read -r group qualifier case_id source_path; do
  if [[ ! $group =~ ^(core|transform|fhe)$ ]] || \
     [[ ! $qualifier =~ ^(software-self-check|blocked-not-issued|waveform-hold|termination-probe-pass|termination-probe-fail)$ ]] || \
     [[ ! $case_id =~ ^[A-Za-z0-9_]+$ ]] || \
     [[ ! $source_path =~ ^src/(00_bringup|01_configuration|02_data_paths|03_compute_instructions|04_composite_instruction_sequences|05_cpu_hpu_structural_connectivity|06_performance|07_full_application)/[^/]+/[^/]+\.c$ ]]; then
    printf 'ERROR: malformed CASE_MANIFEST.tsv row for %s\n' "$case_id" >&2
    exit 2
  fi
  if [[ -n ${manifest_ids[$case_id]:-} ]]; then
    printf 'ERROR: duplicate case_id in CASE_MANIFEST.tsv: %s\n' "$case_id" >&2
    exit 2
  fi
  if [[ -z ${selected_ids[$case_id]:-} ]]; then
    printf 'ERROR: CASE_MANIFEST.tsv contains unselected case_id: %s\n' \
      "$case_id" >&2
    exit 2
  fi
  if [[ ${roster_group[$case_id]} != "$group" || \
        ${roster_qualifier[$case_id]} != "$qualifier" || \
        ${roster_source[$case_id]} != "$source_path" ]]; then
    printf 'ERROR: CASE_MANIFEST.tsv disagrees with canonical roster for %s\n' \
      "$case_id" >&2
    exit 2
  fi
  relative_no_ext=${source_path#src/}
  relative_no_ext=${relative_no_ext%.c}
  expected_elf_by_id[$case_id]="$artifact_root/$group/$relative_no_ext.elf"
  for extension in elf bin txt; do
    if [[ ! -s $artifact_root/$group/$relative_no_ext.$extension ]]; then
      printf 'ERROR: missing %s artifact for %s\n' "$extension" "$case_id" >&2
      exit 2
    fi
  done
  if [[ $qualifier == blocked-not-issued ]]; then
    actual_not_qualified=$((actual_not_qualified + 1))
  fi
  manifest_ids[$case_id]="$qualifier"
  actual_group_counts[$group]=$((actual_group_counts[$group] + 1))
  actual_cases=$((actual_cases + 1))
done < <(tail -n +2 "$case_manifest")

for case_id in "${selected_case_ids[@]}"; do
  if [[ -z ${manifest_ids[$case_id]:-} ]]; then
    printf 'ERROR: CASE_MANIFEST.tsv omits canonical selected case %s\n' \
      "$case_id" >&2
    exit 2
  fi
done

if ((actual_cases != expected_cases ||
     actual_group_counts[core] != selected_group_counts[core] ||
     actual_group_counts[transform] != selected_group_counts[transform] ||
     actual_group_counts[fhe] != selected_group_counts[fhe] ||
     actual_not_qualified != selected_not_qualified)); then
  printf 'ERROR: CASE_MANIFEST.tsv counts disagree with canonical selection\n' >&2
  exit 2
fi

declare -A not_qualified_ids=()
while IFS=$'\t' read -r case_id source_path reason; do
  if [[ ! $case_id =~ ^[A-Za-z0-9_]+$ ]] || \
     [[ ! $source_path =~ ^src/(00_bringup|01_configuration|02_data_paths|03_compute_instructions|04_composite_instruction_sequences|05_cpu_hpu_structural_connectivity|06_performance|07_full_application)/[^/]+/[^/]+\.c$ ]] || \
     [[ -n ${not_qualified_ids[$case_id]:-} ]] || \
     [[ -z ${selected_ids[$case_id]:-} ]] || \
     [[ ${roster_qualifier[$case_id]} != blocked-not-issued ]] || \
     [[ ${roster_source[$case_id]} != "$source_path" ]] || [[ -z $reason ]]; then
    printf 'ERROR: invalid NOT_QUALIFIED.tsv row for %s\n' "$case_id" >&2
    exit 2
  fi
  not_qualified_ids[$case_id]=1
done < <(tail -n +2 "$not_qualified_manifest")
if [[ ${#not_qualified_ids[@]} -ne $selected_not_qualified ]]; then
  printf 'ERROR: NOT_QUALIFIED.tsv count disagrees with canonical selection\n' >&2
  exit 2
fi
for case_id in "${selected_case_ids[@]}"; do
  if [[ ${roster_qualifier[$case_id]} == blocked-not-issued && \
        -z ${not_qualified_ids[$case_id]:-} ]]; then
    printf 'ERROR: NOT_QUALIFIED.tsv omits canonical blocked case %s\n' \
      "$case_id" >&2
    exit 2
  fi
done

mm_artifact="$artifact_root/provenance/inline-asm-mm"
if [[ ! $manifest_inline_asm =~ ^[0-9a-f]{40}$ ]] || \
   [[ ! -s $mm_artifact/PRODUCER_COMMIT ]] || \
   [[ $manifest_inline_asm != "$(<"$mm_artifact/PRODUCER_COMMIT")" ]]; then
  printf 'ERROR: selected inline-asm MM provenance is incomplete\n' >&2
  exit 2
fi
for required in encoder_words.tsv RESOLVED_DMA_SPANS.csv DELIVERY_SUMMARY.md \
                mm.c mm.h mm.asm mm.inst32 dma_relocation_manifest.csv; do
  if [[ ! -s $mm_artifact/$required ]]; then
    printf 'ERROR: selected inline-asm MM provenance omits %s\n' \
      "$required" >&2
    exit 2
  fi
done

encoder_words="$mm_artifact/encoder_words.tsv"
if [[ ! -s $encoder_words ]] || \
   [[ $(sed -n '1p' "$encoder_words") != $'macro_name\tword_hex\tnormalized_asm' ]] || \
   ! awk -F '\t' 'NF != 3 { exit 1 }' "$encoder_words"; then
  printf 'ERROR: producer encoder word table is missing or malformed\n' >&2
  exit 2
fi
declare -A producer_word_seen=()
producer_words=()
while IFS=$'\t' read -r macro_name word_hex normalized_asm; do
  if [[ ! $macro_name =~ ^[A-Za-z_][A-Za-z0-9_]*$ ]] || \
     [[ ! $word_hex =~ ^0[xX][0-9A-Fa-f]{8}$ ]] || \
     [[ -z $normalized_asm ]]; then
    printf 'ERROR: malformed producer encoder word row: %s %s\n' \
      "$macro_name" "$word_hex" >&2
    exit 2
  fi
  word=${word_hex#0x}
  word=${word#0X}
  word=${word,,}
  if [[ -n ${producer_word_seen[$word]:-} ]]; then
    printf 'ERROR: duplicate producer encoder word: %s\n' "$word_hex" >&2
    exit 2
  fi
  producer_word_seen[$word]=$macro_name
  producer_words+=("$word")
done < <(tail -n +2 "$encoder_words")
if [[ ${#producer_words[@]} -eq 0 ]]; then
  printf 'ERROR: producer encoder word table is empty\n' >&2
  exit 2
fi

mapfile -t elfs < <(find "$artifact_root" -type f -name '*.elf' -print | sort)
if [[ ${#elfs[@]} -ne $expected_cases ]]; then
  printf 'ERROR: expected %u ELFs, found %u\n' \
    "$expected_cases" "${#elfs[@]}" >&2
  exit 2
fi

verify_root="$artifact_root/.verify.$$"
mkdir -p "$verify_root"
cleanup() {
  find "$verify_root" -mindepth 1 -delete 2>/dev/null || true
  rmdir "$verify_root" 2>/dev/null || true
}
trap cleanup EXIT HUP INT TERM

require_word() {
  local txt=$1
  local word=$2
  grep -Eiq "(^|[[:space:]])${word}([[:space:]]|$)" "$txt" || {
    printf 'ERROR: %s does not contain instruction word %s\n' "$txt" "$word" >&2
    exit 2
  }
}

reject_generated_hpu_words() {
  local txt=$1
  local word

  for word in "${producer_words[@]}"; do
    if grep -Eiq "(^|[[:space:]])${word}([[:space:]]|$)" "$txt"; then
      printf 'ERROR: blocked testcase contains producer HPU instruction word %s: %s\n' \
        "$word" "$txt" >&2
      exit 2
    fi
  done
}

require_generated_hpu_word() {
  local txt=$1
  local word

  for word in "${producer_words[@]}"; do
    if grep -Eiq "(^|[[:space:]])${word}([[:space:]]|$)" "$txt"; then
      return 0
    fi
  done
  printf 'ERROR: software testcase contains no producer-encoded HPU instruction: %s\n' \
    "$txt" >&2
  exit 2
}

require_generated_mm_stream() {
  local txt=$1
  local inst32="$mm_artifact/mm.inst32"
  local bits
  local word

  if [[ ! -s $inst32 ]]; then
    printf 'ERROR: generated MM instruction stream is missing: %s\n' \
      "$inst32" >&2
    exit 2
  fi
  while IFS= read -r bits; do
    [[ -z $bits ]] && continue
    if [[ ! $bits =~ ^[01]{32}$ ]]; then
      printf 'ERROR: malformed MM inst32 row: %s\n' "$bits" >&2
      exit 2
    fi
    printf -v word '%08x' "$((2#$bits))"
    require_word "$txt" "$word"
  done < "$inst32"
}

require_main_return() {
  local txt=$1
  local value=$2
  grep -Eq "[[:space:]]li[[:space:]]+a0,${value}([[:space:]]|$)" "$txt" || {
    printf 'ERROR: %s does not return %s from main()\n' "$txt" "$value" >&2
    exit 2
  }
}

require_rns_fixture() {
  local elf=$1
  local symbol

  for symbol in hpu_rns_input_a hpu_rns_input_b; do
    "${cross_compile}nm" -S --defined-only "$elf" | grep -Eq \
      "^[[:xdigit:]]+[[:space:]]+0*4000[[:space:]]+[Rr][[:space:]]+${symbol}$" || {
        printf 'ERROR: %s does not embed 16384-byte %s\n' \
          "$elf" "$symbol" >&2
        exit 2
      }
  done
}

require_mm_fixture() {
  local elf=$1
  local symbol

  for symbol in hpu_rns_expected hpu_rns_input_a hpu_rns_input_b; do
    "${cross_compile}nm" -S --defined-only "$elf" | grep -Eq \
      "^[[:xdigit:]]+[[:space:]]+0*4000[[:space:]]+[Rr][[:space:]]+${symbol}$" || {
        printf 'ERROR: %s does not embed 16384-byte %s\n' \
          "$elf" "$symbol" >&2
        exit 2
      }
  done
  "${cross_compile}nm" -S --defined-only "$elf" | grep -Eq \
    '^[[:xdigit:]]+[[:space:]]+0*100[[:space:]]+[Rr][[:space:]]+hpu_rns_mod_ctx$' || {
      printf 'ERROR: %s does not embed the 256-byte modulus context\n' \
        "$elf" >&2
      exit 2
    }
  "${cross_compile}nm" --defined-only "$elf" | grep -Eq \
    '[[:space:]][Tt][[:space:]]+hpu_program_mm$' || {
      printf 'ERROR: %s does not link the producer hpu_program_mm entry\n' \
        "$elf" >&2
      exit 2
    }
}

reject_mm_only_fixture() {
  local elf=$1

  if "${cross_compile}nm" --defined-only "$elf" | grep -Eq \
      '[[:space:]](hpu_rns_expected|hpu_rns_mod_ctx)$'; then
    printf 'ERROR: non-MM testcase embeds MM-only golden/modulus data: %s\n' \
      "$elf" >&2
    exit 2
  fi
}

for elf in "${elfs[@]}"; do
  base=${elf%.elf}
  bin="$base.bin"
  txt="$base.txt"
  name=$(basename "$base")
  if [[ -z ${manifest_ids[$name]:-} ]] || \
     [[ ${expected_elf_by_id[$name]:-} != "$elf" ]]; then
    printf 'ERROR: ELF path is not the canonical artifact path for %s: %s\n' \
      "$name" "$elf" >&2
    exit 2
  fi
  qualifier=${manifest_ids[$name]}
  test -s "$bin" && test -s "$txt" || {
    printf 'ERROR: incomplete ELF/BIN/TXT set: %s\n' "$base" >&2
    exit 2
  }
  "${cross_compile}readelf" -h "$elf" | \
    grep -q 'Machine:[[:space:]]*RISC-V'
  "${cross_compile}readelf" -h "$elf" | \
    grep -q 'Entry point address:[[:space:]]*0x80000000'
  "${cross_compile}nm" --defined-only "$elf" | \
    grep -Eq '[[:space:]][Tt][[:space:]]+main$' || {
      printf 'ERROR: ELF does not define main: %s\n' "$elf" >&2
      exit 2
    }

  rebuilt_bin="$verify_root/$name.bin"
  rebuilt_txt="$verify_root/$name.txt"
  "${cross_compile}objcopy" -O binary "$elf" "$rebuilt_bin"
  cmp "$bin" "$rebuilt_bin"
  (
    cd "$(dirname "$elf")"
    "${cross_compile}objdump" -d "$(basename "$elf")"
  ) > "$rebuilt_txt"
  cmp "$txt" "$rebuilt_txt"

  if [[ $qualifier == blocked-not-issued ]]; then
    reject_generated_hpu_words "$txt"
  elif [[ $qualifier == software-self-check && \
          ${roster_source[$name]} != src/00_bringup/* && \
          $name != HPU_IT_STING_CFG_001 ]]; then
    require_generated_hpu_word "$txt"
  fi

  case "$elf" in
    */001_hpu_smoke/*.elf)
      require_rns_fixture "$elf"
      if [[ $name != 07_dload_compute_dstore_psync ]]; then
        reject_mm_only_fixture "$elf"
      fi ;;
    */01_configuration/*.elf|*/02_data_paths/*.elf|\
    */03_compute_instructions/*.elf|*/04_composite_instruction_sequences/*.elf|\
    */05_cpu_hpu_structural_connectivity/*.elf|*/06_performance/*.elf|\
    */07_full_application/*.elf)
      require_rns_fixture "$elf"
      reject_mm_only_fixture "$elf" ;;
  esac

  case "$name" in
    01_dload_hold|03_dload_poll_mmio)
      require_word "$txt" 00b5102b ;;
    04_dload_psync_poll_mmio|05_dload_psync_irq)
      require_word "$txt" 00b5102b
      require_word "$txt" 7000000b ;;
    06_dload_dstore_psync)
      require_word "$txt" 00b5102b
      require_word "$txt" 00b5502b
      require_word "$txt" 7000000b ;;
    07_dload_compute_dstore_psync)
      require_mm_fixture "$elf"
      require_generated_mm_stream "$txt" ;;
    01_return_0)
      require_main_return "$txt" 0 ;;
    02_return_1)
      require_main_return "$txt" 1 ;;
  esac
done

printf '[hputest] validation PASS: %u ELF/BIN/TXT sets\n' "${#elfs[@]}"
