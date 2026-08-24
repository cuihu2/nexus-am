#!/usr/bin/env bash
set -Eeuo pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
test_root=$(cd -- "$script_dir/.." && pwd)
inline_asm_root=${1:-"$test_root/third_party/inline-asm"}
generated_root=${HPU_GENERATED_ROOT:-"$test_root/build/generated"}
encoding_header="$generated_root/include/hpu/inline_asm_mm_delivery.h"

if [[ ! -f $inline_asm_root/encode/include/assembler.hpp ]] || \
   ! git -C "$inline_asm_root" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
  echo 'ERROR: inline-asm submodule is not initialized' >&2
  echo 'run: git submodule update --init --recursive tests/hputest/third_party/inline-asm' >&2
  exit 2
fi
if [[ ! -s $encoding_header ]]; then
  echo 'ERROR: generated inline-asm delivery header is missing' >&2
  echo 'run: make -C tests/hputest prepare-inline-asm-mm' >&2
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
if ! git -C "$inline_asm_root" diff --quiet -- || \
   ! git -C "$inline_asm_root" diff --cached --quiet --; then
  echo 'ERROR: inline-asm submodule contains tracked modifications' >&2
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
  -I"$generated_root/include" \
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
