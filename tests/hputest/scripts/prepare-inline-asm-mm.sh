#!/usr/bin/env bash
set -Eeuo pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
test_root=$(cd -- "$script_dir/.." && pwd)
inline_asm_root=${1:-"$test_root/third_party/inline-asm"}
output_root=${2:-"$test_root/build"}
jobs=${3:-${JOBS:-4}}

if [[ $inline_asm_root != /* ]]; then
  inline_asm_root=$(cd -- "$test_root" && realpath -m -- "$inline_asm_root")
fi
if [[ $output_root != /* ]]; then
  output_root=$(cd -- "$test_root" && realpath -m -- "$output_root")
fi
if [[ ! $jobs =~ ^[1-9][0-9]*$ ]]; then
  echo "ERROR: JOBS must be a positive integer: $jobs" >&2
  exit 2
fi
if [[ ! -f $inline_asm_root/CMakeLists.txt ]] || \
   ! git -C "$inline_asm_root" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
  echo 'ERROR: inline-asm submodule is not initialized' >&2
  echo 'run: git submodule update --init --recursive tests/hputest/third_party/inline-asm' >&2
  exit 2
fi
if ! git -C "$inline_asm_root" diff --quiet -- || \
   ! git -C "$inline_asm_root" diff --cached --quiet --; then
  echo 'ERROR: inline-asm submodule contains tracked modifications' >&2
  exit 2
fi

repository_root=$(git -C "$test_root" rev-parse --show-toplevel)
submodule_path=${inline_asm_root#"$repository_root/"}
gitlink_commit=$(git -C "$repository_root" ls-files -s -- "$submodule_path" |
  awk '$1 == "160000" { print $2 }')
if [[ -z $gitlink_commit ]]; then
  echo "ERROR: inline-asm root is not the tracked Nexus-AM submodule" >&2
  exit 2
fi
producer_commit=$(git -C "$inline_asm_root" rev-parse HEAD)
if [[ $producer_commit != "$gitlink_commit" ]]; then
  echo 'ERROR: inline-asm checkout does not match the Nexus-AM gitlink' >&2
  echo "gitlink:  $gitlink_commit" >&2
  echo "checkout: $producer_commit" >&2
  exit 2
fi
for tool in cmake python3 "${CXX:-c++}" sha256sum; do
  command -v "$tool" >/dev/null || {
    echo "ERROR: missing build tool: $tool" >&2
    exit 2
  }
done

cmake_build="$output_root/inline-asm-cmake"
generated_root="$output_root/generated"
import_root="$generated_root/inline-asm/mm"
generated_header="$generated_root/include/hpu/inline_asm_mm_delivery.h"
tool_root="$output_root/inline-asm-tools"
encoding_tsv="$tool_root/encoder_words.tsv"
stamp="$generated_root/.inline-asm-mm-producer"

mkdir -p -- "$cmake_build" "$tool_root" "$generated_root"
fingerprint=$(
  {
    printf '%s\n' "$producer_commit"
    sha256sum \
      "$script_dir/prepare-inline-asm-mm.sh" \
      "$script_dir/import-inline-asm-mm.py" \
      "$script_dir/generate-inline-asm-encodings.cpp"
  } | sha256sum | awk '{print $1}'
)

required_outputs=(
  "$inline_asm_root/outputs/mm/mm.c"
  "$inline_asm_root/outputs/mm/mm.h"
  "$inline_asm_root/outputs/mm/mm.asm"
  "$inline_asm_root/outputs/mm/dma_relocation_manifest.csv"
  "$inline_asm_root/outputs/mm/test_data/params.json"
  "$inline_asm_root/outputs/mm/test_data/hardware/line_map.csv"
  "$inline_asm_root/outputs/mm/test_data/hardware/images/input_a.u32.bin"
  "$inline_asm_root/outputs/mm/test_data/hardware/images/input_b.u32.bin"
  "$inline_asm_root/outputs/mm/test_data/hardware/images/expected.u32.bin"
  "$inline_asm_root/outputs/mm/test_data/hardware/constants/mod_ctx.u32.bin"
)
need_delivery=0
if [[ ! -f $stamp ]] || [[ $(<"$stamp") != "$fingerprint" ]]; then
  need_delivery=1
fi
for path in "${required_outputs[@]}"; do
  if [[ ! -s $path ]]; then
    need_delivery=1
  fi
done

if ((need_delivery)); then
  echo "[hputest] generating inline-asm delivery at $producer_commit"
  cmake -S "$inline_asm_root" -B "$cmake_build" \
    -DBUILD_TESTING=ON \
    -DCMAKE_BUILD_TYPE=Release
  cmake --build "$cmake_build" --parallel "$jobs" --target hpu_delivery
else
  echo "[hputest] reuse inline-asm delivery for $producer_commit"
fi

"${CXX:-c++}" \
  -std=c++17 -Wall -Wextra -Werror \
  -I"$inline_asm_root/encode/include" \
  "$script_dir/generate-inline-asm-encodings.cpp" \
  "$inline_asm_root/encode/src/instruction.cpp" \
  "$inline_asm_root/encode/src/parser.cpp" \
  "$inline_asm_root/encode/src/encoder.cpp" \
  "$inline_asm_root/encode/src/assembler.cpp" \
  -o "$tool_root/generate-inline-asm-encodings"
"$tool_root/generate-inline-asm-encodings" "$encoding_tsv"

python3 "$script_dir/import-inline-asm-mm.py" \
  --source "$inline_asm_root/outputs/mm" \
  --destination "$import_root" \
  --header "$generated_header" \
  --encodings "$encoding_tsv" \
  --producer-commit "$producer_commit"

printf '%s\n' "$fingerprint" > "$stamp"
echo '[hputest] inline-asm MM generation/import PASS'
