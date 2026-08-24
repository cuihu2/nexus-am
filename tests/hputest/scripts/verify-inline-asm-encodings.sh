#!/usr/bin/env bash
set -Eeuo pipefail

inline_asm_root=${1:-}
script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
test_root=$(cd -- "$script_dir/.." && pwd)
encoding_header="$test_root/include/hpu/encoding.h"

if [[ -z $inline_asm_root || ! -d $inline_asm_root/.git ]]; then
  echo 'ERROR: path to a cuihu2/inline-asm checkout is required' >&2
  exit 2
fi

expected_commit=$(sed -n \
  's/^#define HPU_INLINE_ASM_SOURCE_COMMIT "\([0-9a-f]\{40\}\)"$/\1/p' \
  "$encoding_header")
actual_commit=$(git -C "$inline_asm_root" rev-parse HEAD)
if [[ -z $expected_commit || $actual_commit != "$expected_commit" ]]; then
  echo "ERROR: inline-asm commit mismatch" >&2
  echo "expected: $expected_commit" >&2
  echo "actual:   $actual_commit" >&2
  exit 2
fi

mkdir -p "$test_root/build"
build_dir=$(mktemp -d "$test_root/build/inline-asm-check.XXXXXX")
cleanup() {
  rm -rf -- "$build_dir"
}
trap cleanup EXIT HUP INT TERM

"${CXX:-c++}" \
  -std=c++17 -Wall -Wextra -Werror \
  -I"$test_root/include" \
  -I"$inline_asm_root/encode/include" \
  "$script_dir/check-inline-asm-encodings.cpp" \
  "$inline_asm_root/encode/src/instruction.cpp" \
  "$inline_asm_root/encode/src/parser.cpp" \
  "$inline_asm_root/encode/src/encoder.cpp" \
  "$inline_asm_root/encode/src/assembler.cpp" \
  -o "$build_dir/check-inline-asm-encodings"

"$build_dir/check-inline-asm-encodings"
echo '[hputest] pinned inline-asm encodings PASS'
