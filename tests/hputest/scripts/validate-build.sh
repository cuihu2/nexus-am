#!/usr/bin/env bash
set -Eeuo pipefail

artifact_root=${1:-}
expected_cases=${EXPECTED_CASES:-7}
cross_compile=${CROSS_COMPILE:-riscv64-linux-gnu-}

if [[ -z $artifact_root || ! -d $artifact_root ]]; then
  printf 'ERROR: artifact directory is required\n' >&2
  exit 2
fi
if [[ ! $expected_cases =~ ^[1-9][0-9]*$ ]]; then
  printf 'ERROR: EXPECTED_CASES must be positive\n' >&2
  exit 2
fi
for tool in objcopy objdump readelf; do
  command -v "${cross_compile}${tool}" >/dev/null || exit 2
done

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

for elf in "${elfs[@]}"; do
  base=${elf%.elf}
  bin="$base.bin"
  txt="$base.txt"
  name=$(basename "$base")
  test -s "$bin" && test -s "$txt" || {
    printf 'ERROR: incomplete ELF/BIN/TXT set: %s\n' "$base" >&2
    exit 2
  }
  "${cross_compile}readelf" -h "$elf" | \
    grep -q 'Machine:[[:space:]]*RISC-V'
  "${cross_compile}readelf" -h "$elf" | \
    grep -q 'Entry point address:[[:space:]]*0x80000000'

  rebuilt_bin="$verify_root/$name.bin"
  rebuilt_txt="$verify_root/$name.txt"
  "${cross_compile}objcopy" -O binary "$elf" "$rebuilt_bin"
  cmp "$bin" "$rebuilt_bin"
  (
    cd "$(dirname "$elf")"
    "${cross_compile}objdump" -d "$(basename "$elf")"
  ) > "$rebuilt_txt"
  cmp "$txt" "$rebuilt_txt"

  case "$name" in
    01_dload_hold|03_dload_poll_csr)
      require_word "$txt" 00b5102b ;;
    04_psync_irq)
      require_word "$txt" 7000000b ;;
    05_dload_psync)
      require_word "$txt" 00b5102b
      require_word "$txt" 7000000b ;;
    06_dload_dstore_psync)
      require_word "$txt" 00b5102b
      require_word "$txt" 00b5502b
      require_word "$txt" 7000000b ;;
    07_dload_compute_dstore_psync)
      require_word "$txt" 00b5292b
      require_word "$txt" 00b5102b
      require_word "$txt" 00b5122b
      require_word "$txt" 6000000b
      require_word "$txt" 0400400b
      require_word "$txt" 00b5542b
      require_word "$txt" 7000000b ;;
  esac
done

test -s "$artifact_root/MANIFEST.txt"
test -s "$artifact_root/SHA256SUMS"
(cd "$artifact_root" && sha256sum -c SHA256SUMS >/dev/null)
printf '[hputest] validation PASS: %u ELF/BIN/TXT sets\n' "${#elfs[@]}"
