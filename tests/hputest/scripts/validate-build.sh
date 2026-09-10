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

if [[ ${#roster_ids[@]} -ne 60 || ${roster_group_counts[core]} -ne 39 || \
      ${roster_group_counts[transform]} -ne 8 || \
      ${roster_group_counts[fhe]} -ne 13 || $roster_migrated -ne 49 || \
      $roster_migrated_software -ne 29 || $roster_migrated_blocked -ne 20 || \
      ${roster_qualifier_counts[software-self-check]} -ne 37 || \
      ${roster_qualifier_counts[blocked-not-issued]} -ne 20 || \
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
  source_file="$test_root/$source_path"
  qualifier=${roster_qualifier[$case_id]}
  if ! grep -Fq '#include <hpu/result.h>' "$source_file" ||
     ! grep -Fq 'case_start(__FILE__);' "$source_file"; then
    printf 'ERROR: testcase lacks UART start reporting: %s\n' \
      "$source_path" >&2
    exit 2
  fi
  case "$qualifier" in
    software-self-check)
      if ! grep -Fq 'case_pass(__FILE__)' "$source_file" ||
         { ! grep -Fq 'case_fail(__FILE__, __LINE__)' "$source_file" &&
           ! { grep -Fq 'failure(__LINE__,' "$source_file" &&
               grep -Eq 'case_fail\(__FILE__, (line|source_line)\)' "$source_file"; }; }; then
        printf 'ERROR: self-check testcase lacks PASS/FAIL reporting: %s\n' \
          "$source_path" >&2
        exit 2
      fi ;;
    blocked-not-issued)
      if ! grep -Fq 'case_not_qualified(__FILE__);' "$source_file" ||
         grep -Fq 'case_pass(__FILE__)' "$source_file"; then
        printf 'ERROR: blocked testcase has incorrect result reporting: %s\n' \
          "$source_path" >&2
        exit 2
      fi ;;
    waveform-hold)
      if ! grep -Fq 'case_hold(__FILE__);' "$source_file" ||
         grep -Fq 'case_pass(__FILE__)' "$source_file"; then
        printf 'ERROR: waveform-hold testcase has incorrect reporting: %s\n' \
          "$source_path" >&2
        exit 2
      fi ;;
    termination-probe-pass)
      grep -Fq '(void)case_pass(__FILE__);' "$source_file" || {
        printf 'ERROR: return-zero probe lacks PASS reporting: %s\n' \
          "$source_path" >&2
        exit 2
      } ;;
    termination-probe-fail)
      grep -Fq 'case_expected_failure(__FILE__);' "$source_file" || {
        printf 'ERROR: return-one probe lacks expected-failure reporting: %s\n' \
          "$source_path" >&2
        exit 2
      } ;;
  esac
  [[ $source_path == src/00_bringup/* ]] && continue
  if grep -Eq '^#define (CASE_ID|TESTPOINT|DESCRIPTION|TEST_MODE|PRIORITY|CASE_KIND|REQUIREMENTS|SEED)([[:space:]]|$)' \
       "$source_file"; then
    printf 'ERROR: migrated testcase contains obsolete metadata macros: %s\n' \
      "$source_path" >&2
    exit 2
  fi
  if ! grep -Fq ' * 测试点：' "$source_file" || \
     ! grep -Fq ' * 目的：' "$source_file"; then
    printf 'ERROR: migrated testcase lacks its readable testpoint/purpose comment: %s\n' \
      "$source_path" >&2
    exit 2
  fi
  if grep -Eq 'hpu_it_|<hpu/it_case_steps\.h>' "$source_file"; then
    printf 'ERROR: migrated testcase uses the retired hpu_it_* interface: %s\n' \
      "$source_path" >&2
    exit 2
  fi
  source_code=$("${cross_compile}gcc" -fpreprocessed -E -P "$source_file")
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
manifest_uart=$(manifest_value uart_results)
manifest_dump=$(manifest_value hpu_dump_results)
if [[ $manifest_uart:$manifest_dump != brief:0 && $manifest_uart:$manifest_dump != full:1 ]]; then
  printf 'ERROR: inconsistent UART result mode in MANIFEST\n' >&2
  exit 2
fi
if [[ $manifest_selection == diagnostic && $manifest_uart != full ]]; then
  printf 'ERROR: diagnostic selection requires full UART results\n' >&2
  exit 2
fi

declare -A selected_ids=()
declare -A selected_group_counts=([core]=0 [transform]=0 [fhe]=0)
selected_case_ids=()
selected_not_qualified=0
case "$manifest_selection" in
  all|core|transform|fhe|diagnostic)
    for case_id in "${roster_ids[@]}"; do
      group=${roster_group[$case_id]}
      if [[ $manifest_selection == diagnostic ]]; then
        [[ ${roster_source[$case_id]} == src/03_compute_instructions/* || \
           $case_id =~ ^HPU_IT_DIR_CMB_00[123]$ ]] || continue
      elif [[ $manifest_selection != all && $group != "$manifest_selection" ]]; then
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
                mm.c mm.h mm.asm mm.inst32 mm.cmd26 dma_relocation_manifest.csv \
                opcode_map.csv upstream/mm.c upstream/mm.h upstream/mm.asm \
                upstream/mm.inst32 upstream/mm.cmd26 upstream/encoder_words.tsv \
                upstream/dma_relocation_manifest.csv; do
  if [[ ! -s $mm_artifact/$required ]]; then
    printf 'ERROR: selected inline-asm MM provenance omits %s\n' \
      "$required" >&2
    exit 2
  fi
done
# 独立核对交付前后的机器码，而非仅相信生成器与头文件之间的同源比较。
# 此检查只处理来源明确的 HPU 交付文件，不修改或限制 AM/DASICS 的 custom0。
python3 - "$mm_artifact" <<'PY'
import csv
from pathlib import Path
import re
import sys

root = Path(sys.argv[1])
raw = root / "upstream"


def require(condition, message):
    if not condition:
        raise SystemExit("ERROR: HPU opcode mapping provenance: " + message)


def mapped(word):
    opcode = word & 0x7f
    require(opcode in (0x0b, 0x2b), "unexpected upstream HPU opcode")
    return (word & ~0x7f) | 0x5b if opcode == 0x0b else word


def binary_words(path, width):
    lines = path.read_text().splitlines()
    require(bool(lines) and all(re.fullmatch(r"[01]{%d}" % width, line)
                                for line in lines), str(path))
    return [int(line, 2) for line in lines]


source = binary_words(raw / "mm.inst32", 32)
target = binary_words(root / "mm.inst32", 32)
commands = binary_words(root / "mm.cmd26", 26)
require(len(source) == 10 and source[-1] == 0x7000000b,
        "raw MM stream must retain ten instructions ending in PSYNC")
require(target == [mapped(word) for word in source], "mm.inst32 mapping")
require(commands == binary_words(raw / "mm.cmd26", 26), "cmd26 changed")
require(commands == [((word & 0x7f) == 0x2b) << 25 | (word >> 7)
                     for word in target], "payload/cmd_kind changed")
for directory, words in ((raw, source), (root, target)):
    c_words = [int(word, 16) for word in re.findall(
        r"\.word\s+0x([0-9a-fA-F]{8})", (directory / "mm.c").read_text())]
    require(c_words == words, str(directory / "mm.c"))
def hide_words(text):
    return re.sub(r"(\.word\s+)0x[0-9a-fA-F]{8}", r"\1<word>", text)
require(hide_words((raw / "mm.c").read_text())
        == hide_words((root / "mm.c").read_text()),
        "non-opcode C text changed (GPR binding or OBJ.len checks)")

with (root / "opcode_map.csv").open(newline="") as stream:
    reader = csv.DictReader(stream)
    require(reader.fieldnames == ["instruction_index", "source_word",
                                  "target_word", "cmd_kind", "cmd26"],
            "opcode_map.csv header")
    rows = list(reader)
require(len(rows) == len(source), "opcode_map.csv count")
for index, row in enumerate(rows):
    require(int(row["instruction_index"]) == index
            and int(row["source_word"], 16) == source[index]
            and int(row["target_word"], 16) == target[index]
            and int(row["cmd_kind"]) == (commands[index] >> 25)
            and int(row["cmd26"], 16) == commands[index], "opcode_map.csv row")

tables = []
for directory in (raw, root):
    with (directory / "encoder_words.tsv").open(newline="") as stream:
        rows = list(csv.reader(stream, delimiter="\t"))
    require(rows and rows[0] == ["macro_name", "word_hex", "normalized_asm"]
            and all(len(row) == 3 for row in rows), "encoder_words.tsv format")
    tables.append(rows[1:])
require(bool(tables[0]) and len(tables[0]) == len(tables[1]), "primitive count")
for before, after in zip(*tables):
    require(before[0] == after[0] and before[2] == after[2]
            and mapped(int(before[1], 16)) == int(after[1], 16),
            "primitive mapping for " + before[0])
for name in ("mm.h", "mm.asm", "mm.cmd26", "dma_relocation_manifest.csv"):
    require((raw / name).read_bytes() == (root / name).read_bytes(),
            "non-opcode input changed: " + name)
PY
# 08/09 直接调用生成器的完整程序，不再交付插入额外 PSYNC 的旧分阶段程序。
if [[ -e $mm_artifact/mm_phases.c || -e $mm_artifact/mm_phases.h ]]; then
  printf 'ERROR: obsolete AM MM phases remain in producer provenance\n' >&2
  exit 2
fi
for register in x10 x11; do
  binding_count=$(grep -Ec \
    "register uintptr_t hpu_rs[12] __asm__\(\"${register}\"\) = \(uintptr_t\)spans\[[0-3]\]\.line_(offset|count);" \
    "$mm_artifact/mm.c" || true)
  if [[ $binding_count -ne 4 ]]; then
    printf 'ERROR: generated MM program must retain four DMA %s bindings\n' \
      "$register" >&2
    exit 2
  fi
done
for span in 0 1 2 3; do
  if ! grep -Fq \
      "register uintptr_t hpu_rs1 __asm__(\"x10\") = (uintptr_t)spans[$span].line_offset;" \
      "$mm_artifact/mm.c" || \
     ! grep -Fq \
      "register uintptr_t hpu_rs2 __asm__(\"x11\") = (uintptr_t)spans[$span].line_count;" \
      "$mm_artifact/mm.c"; then
    printf 'ERROR: generated MM DMA span %u has changed offset/count GPR bindings\n' \
      "$span" >&2
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
declare -A producer_word_by_macro=()
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
  if [[ -n ${producer_word_seen[$word]:-} || \
        -n ${producer_word_by_macro[$macro_name]:-} ]]; then
    printf 'ERROR: duplicate producer encoder word or macro: %s %s\n' \
      "$word_hex" "$macro_name" >&2
    exit 2
  fi
  producer_word_seen[$word]=$macro_name
  producer_word_by_macro[$macro_name]=$word
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
  local expected_words=()
  local actual_words=()

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
    expected_words+=("$word")
  done < "$inst32"
  if [[ ${#expected_words[@]} -ne 10 || ${expected_words[9]} != 7000005b ]]; then
    printf 'ERROR: producer MM stream must have ten commands ending in PSYNC\n' >&2
    exit 2
  fi
  # 只提取实际链接的上游函数，逐条比较，不能只证明每个字在 ELF 中出现过。
  while IFS= read -r word; do
    if (( (16#$word & 127) == 91 || (16#$word & 127) == 43 )); then
      actual_words+=("$word")
    fi
  done < <(awk '
    /<hpu_program_mm>:/ { in_program = 1; next }
    in_program && /^[[:xdigit:]]+ <[^>]+>:/ { in_program = 0 }
    in_program && /^[[:space:]]*[[:xdigit:]]+:/ && length($2) == 8 {
      print tolower($2)
    }
  ' "$txt")
  if [[ ${#actual_words[@]} -ne 10 || ${actual_words[*]} != "${expected_words[*]}" ]]; then
    printf 'ERROR: linked hpu_program_mm differs from producer instruction stream: %s\n' \
      "$txt" >&2
    exit 2
  fi
  local sync_count
  sync_count=$(grep -Eic '^[[:space:]]*[[:xdigit:]]+:[[:space:]]+7000005b[[:space:]]' "$txt" || true)
  if [[ $sync_count -ne 1 ]]; then
    printf 'ERROR: MM testcase must contain only the producer final PSYNC: %s\n' "$txt" >&2
    exit 2
  fi
}

reject_old_hpu_opcode() {
  local txt=$1
  local word

  # 扫描 HPU 用例和发射函数（含编译器克隆），不全局禁止合法 DASICS 0x0B。
  while IFS= read -r word; do
    if (( (16#$word & 127) == 11 )); then
      printf 'ERROR: HPU testcase contains legacy custom0 instruction %s: %s\n' \
        "$word" "$txt" >&2
      exit 2
    fi
  done < <(awk '
    /^[[:xdigit:]]+ <[^>]+>:/ {
      in_hpu = ($0 ~ /<(main|hpu_program_(mm|ntt|intt|bconv)|psync|hpu_psync|pmodld|hpu_pmodld_0|padd|hpu_padd_p2_p0_p1|psub|pmul|pmac|pmac_imm|issue_transform|pntt_stage|pintt_stage|pfree|op_[[:alnum:]_]+)(\.[^>]*)?>:/)
      next
    }
    in_hpu && /^[[:space:]]*[[:xdigit:]]+:/ && length($2) == 8 {
      print tolower($2)
    }
  ' "$txt")
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

  for symbol in RNS_A RNS_B; do
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

  for symbol in RNS_EXPECTED RNS_A RNS_B; do
    "${cross_compile}nm" -S --defined-only "$elf" | grep -Eq \
      "^[[:xdigit:]]+[[:space:]]+0*4000[[:space:]]+[Rr][[:space:]]+${symbol}$" || {
        printf 'ERROR: %s does not embed 16384-byte %s\n' \
          "$elf" "$symbol" >&2
        exit 2
      }
  done
  "${cross_compile}nm" -S --defined-only "$elf" | grep -Eq \
    '^[[:xdigit:]]+[[:space:]]+0*100[[:space:]]+[Rr][[:space:]]+RNS_MOD_CTX$' || {
      printf 'ERROR: %s does not embed the 256-byte modulus context\n' \
        "$elf" >&2
      exit 2
    }
  "${cross_compile}nm" --defined-only "$elf" | grep -Eq \
    '[[:space:]][Tt][[:space:]]+hpu_program_mm$' || {
      printf 'ERROR: %s does not link the upstream MM program\n' "$elf" >&2
      exit 2
    }
  if "${cross_compile}nm" --defined-only "$elf" | grep -Eq \
      '[[:space:]](mm_load_mod|mm_compute)$'; then
    printf 'ERROR: obsolete AM MM phase is linked: %s\n' "$elf" >&2
    exit 2
  fi
}

require_stage_fixture() {
  local elf=$1
  local txt=$2
  local direction=$3
  local symbols
  local symbol
  local stage
  local prefix
  local macro_name
  local word
  local emitter_words

  symbols=$("${cross_compile}nm" -S --defined-only "$elf")
  for stage in 0 1 11; do
    symbol="${direction}_twiddle_${stage}"
    if ! grep -Eq \
        "^[[:xdigit:]]+[[:space:]]+0*2000[[:space:]]+[Rr][[:space:]]+${symbol}$" \
        <<< "$symbols"; then
      printf 'ERROR: single-stage ELF lacks 8192-byte read-only %s: %s\n' \
        "$symbol" "$elf" >&2
      exit 2
    fi
  done
  if ! grep -Eq "[[:space:]][Tt][[:space:]]+op_${direction}(\\.[^[:space:]]*)?$" \
      <<< "$symbols" || \
     grep -Eq '[[:space:]][Tt][[:space:]]+not_issued(\.[^[:space:]]*)?$' \
      <<< "$symbols"; then
    printf 'ERROR: single-stage ELF is a placeholder or has no op_%s emitter: %s\n' \
      "$direction" "$elf" >&2
    exit 2
  fi

  # 只在实际 op_ntt/op_intt 发射函数中找指令，不能把数据区中碰巧相同的字当作发令。
  emitter_words=$(awk -v name="op_${direction}" '
    /^[[:xdigit:]]+ <[^>]+>:/ {
      in_emitter = ($0 ~ ("<" name "(\\.[^>]*)?>:"))
      next
    }
    in_emitter && /^[[:space:]]*[[:xdigit:]]+:/ && length($2) == 8 {
      print tolower($2)
    }
  ' "$txt")
  for prefix in "HPU_INSN_P${direction^^}_STAGE" \
                "HPU_INSN_P${direction^^}_P2_P3_STAGE"; do
    for stage in 0 1 11; do
      macro_name="${prefix}${stage}"
      word=${producer_word_by_macro[$macro_name]:-}
      if [[ -z $word ]] || ! grep -Fxq "$word" <<< "$emitter_words"; then
        printf 'ERROR: single-stage emitter omits producer instruction %s (%s): %s\n' \
          "$macro_name" "${word:-missing-provenance}" "$txt" >&2
        exit 2
      fi
    done
  done
  require_word "$txt" 7000005b
}

reject_mm_only_fixture() {
  local elf=$1

  if "${cross_compile}nm" --defined-only "$elf" | grep -Eq \
      '[[:space:]](RNS_EXPECTED|RNS_MOD_CTX)$'; then
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
  reject_old_hpu_opcode "$txt"

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
      if [[ $name != 08_dload_compute_dstore_psync_irq && \
            $name != 09_dload_compute_dstore_poll_mmio ]]; then
        reject_mm_only_fixture "$elf"
      fi ;;
    */01_configuration/*.elf|*/02_data_paths/*.elf|\
    */03_compute_instructions/*.elf|*/04_composite_instruction_sequences/*.elf|\
    */05_cpu_hpu_structural_connectivity/*.elf|*/06_performance/*.elf|\
    */07_full_application/*.elf)
      if [[ $qualifier != blocked-not-issued && $name != HPU_IT_DIR_CMB_001 && \
            $name != HPU_IT_DIR_CMB_002 && \
            $name != HPU_IT_DIR_CMB_003 ]]; then
        require_rns_fixture "$elf"
      fi
      reject_mm_only_fixture "$elf" ;;
  esac

  case "$name" in
    01_dload_hold|03_dload_poll_mmio)
      require_word "$txt" 00b5202b ;;
    04_psync_irq)
      require_word "$txt" 7000005b ;;
    05_dload_psync_irq)
      require_word "$txt" 00b5202b
      require_word "$txt" 7000005b ;;
    06_dload_dstore_poll_mmio)
      require_word "$txt" 00b5202b
      require_word "$txt" 00b5502b
      # 06 必须是真正的无 PSYNC 状态轮询，不能因构建成功而漏掉测试意图。
      if grep -Eq '^[[:space:]]*[[:xdigit:]]+:[[:space:]]+700000(0b|5b)([[:space:]]|$)' "$txt"; then
        printf 'ERROR: pure MMIO case 06 contains PSYNC: %s\n' "$txt" >&2
        exit 2
      fi ;;
    07_dload_dstore_psync_irq)
      require_word "$txt" 00b5202b
      require_word "$txt" 00b5502b
      require_word "$txt" 7000005b ;;
    08_dload_compute_dstore_psync_irq|09_dload_compute_dstore_poll_mmio)
      require_mm_fixture "$elf"
      require_generated_mm_stream "$txt" ;;
    HPU_IT_DIR_INS_C0_005)
      require_stage_fixture "$elf" "$txt" ntt ;;
    HPU_IT_DIR_INS_C0_006)
      require_stage_fixture "$elf" "$txt" intt ;;
    HPU_IT_DIR_CMB_002|HPU_IT_DIR_CMB_003)
      operator=ntt
      [[ $name == HPU_IT_DIR_CMB_003 ]] && operator=intt
      python3 "$script_dir/verify-operator-elf.py" \
        --delivery "$artifact_root/provenance/transform-data/$operator" \
        --elf "$elf" --disassembly "$txt" --operator "$operator" ;;
    HPU_IT_DIR_CMB_001)
      python3 "$script_dir/verify-operator-elf.py" \
        --delivery "$artifact_root/provenance/bconv-data" \
        --elf "$elf" --disassembly "$txt" --operator bconv ;;
    01_return_0)
      require_main_return "$txt" 0 ;;
    02_return_1)
      require_main_return "$txt" 1 ;;
  esac
done

printf '[hputest] validation PASS: %u ELF/BIN/TXT sets\n' "${#elfs[@]}"
