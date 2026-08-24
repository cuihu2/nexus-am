#!/usr/bin/env bash
set -Eeuo pipefail

script_dir=$(cd "$(dirname "$0")" && pwd)
test_root=$(cd "$script_dir/.." && pwd)
output_root=${1:-"$test_root/build"}
case_filter=${2:-}
jobs=${JOBS:-4}
arch=${ARCH:-riscv64-xs}
cross_compile=${CROSS_COMPILE:-riscv64-linux-gnu-}

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

for tool in gcc objcopy objdump readelf strip; do
  command -v "${cross_compile}${tool}" >/dev/null || {
    printf 'ERROR: missing %s%s\n' "$cross_compile" "$tool" >&2
    exit 2
  }
done

if [[ -n $case_filter ]]; then
  case_filter=${case_filter#src/}
  case_filter=${case_filter%.c}
  case_sources=("$test_root/src/$case_filter.c")
else
  mapfile -t case_sources < <(
    find "$test_root/src" -type f -name '*.c' -print | sort
  )
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

for source in "${case_sources[@]}"; do
  relative=${source#"$test_root/src/"}
  relative_no_ext=${relative%.c}
  case_id=$(basename "$relative_no_ext")
  binary="$artifact_root/$relative_no_ext"
  object_dir="$object_root/$relative_no_ext"
  mkdir -p "$(dirname "$binary")" "$object_dir"

  printf '[hputest] build %s\n' "$relative_no_ext"
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
mm_delivery_artifact="$artifact_root/inline-asm-mm"
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
{
  printf 'repository=%s\n' "${GITHUB_REPOSITORY:-local}"
  printf 'revision=%s\n' "$(git -C "$AM_HOME" rev-parse HEAD 2>/dev/null || printf unknown)"
  printf 'arch=%s\n' "$arch"
  printf 'toolchain=%s\n' "$("${cross_compile}gcc" --version | sed -n '1p')"
  printf 'case_count=%u\n' "${#case_sources[@]}"
  printf 'inline_asm_commit=%s\n' "$(<"$mm_delivery_source/PRODUCER_COMMIT")"
} > "$artifact_root/MANIFEST.txt"

EXPECTED_CASES=${#case_sources[@]} CROSS_COMPILE="$cross_compile" \
  "$script_dir/validate-build.sh" "$artifact_root"
printf '[hputest] PASS: %u testcase artifact sets in %s\n' \
  "${#case_sources[@]}" "$artifact_root"
