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
for tool in cmake python3 "${CXX:-c++}"; do
  command -v "$tool" >/dev/null || {
    echo "ERROR: missing build tool: $tool" >&2
    exit 2
  }
done

cmake_build="$output_root/inline-asm-cmake"
# 按生产者提交隔离输出，切换分支时不读取旧 submodule 内的 outputs。
producer_work="$output_root/inline-asm-producer/$producer_commit"
producer_mm="$producer_work/outputs/mm"
generated_root="$output_root/generated"
import_root="$generated_root/inline-asm/mm"
generated_header="$generated_root/include/hpu/inline_asm_mm_delivery.h"
tool_root="$output_root/inline-asm-tools"
encoding_tsv="$tool_root/encoder_words.tsv"

mkdir -p -- "$cmake_build" "$tool_root" "$generated_root" "$producer_work"
required_outputs=(
  "$producer_mm/mm.c"
  "$producer_mm/mm.h"
  "$producer_mm/mm.asm"
  "$producer_mm/dma_relocation_manifest.csv"
  "$producer_mm/test_data/params.json"
  "$producer_mm/test_data/hardware/line_map.csv"
  "$producer_mm/test_data/hardware/images/input_a.u32.bin"
  "$producer_mm/test_data/hardware/images/input_b.u32.bin"
  "$producer_mm/test_data/hardware/images/expected.u32.bin"
  "$producer_mm/test_data/hardware/constants/mod_ctx.u32.bin"
)

echo "[hputest] generating selected inline-asm MM inputs at $producer_commit"
cmake -S "$inline_asm_root" -B "$cmake_build" \
  -DBUILD_TESTING=ON \
  -DHPU_ENABLE_SEAL_DIFFERENTIAL_ORACLE=OFF \
  -DHPU_ENABLE_SEAL_BFV_ORACLE=OFF \
  -DCMAKE_BUILD_TYPE=Release
cmake --build "$cmake_build" --parallel "$jobs" --target \
  inline_asm_codegen inline_asm_encode_outputs hpu_reference_vectors \
  hpu_encode_self_test hpu_ntt_hardware_model_test
# 先验证生产者的 STG/DMA 固定向量与生成 C 的机器码，再接收交付。
"$cmake_build/test/encode/hpu_encode_self_test"
"$cmake_build/test/reference/hpu_ntt_hardware_model_test"
(
  cd "$producer_work"
  "$cmake_build/inline_asm_codegen" both \
    --config "$inline_asm_root/config/fhe_test.conf"
  "$cmake_build/test/encode/inline_asm_encode_outputs" \
    --config "$inline_asm_root/config/fhe_test.conf"
  "$cmake_build/test/reference/hpu_reference_vectors" \
    "$producer_work/outputs/ciphertext_multiply/test_data" \
    "$producer_work/outputs" \
    --config "$inline_asm_root/config/fhe_test.conf"
)
for path in "${required_outputs[@]}"; do
  if [[ ! -s $path ]]; then
    echo "ERROR: inline-asm generation omitted required MM output: $path" >&2
    exit 2
  fi
done

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
  --source "$producer_mm" \
  --destination "$import_root" \
  --header "$generated_header" \
  --encodings "$encoding_tsv" \
  --producer-commit "$producer_commit"

python3 "$script_dir/import-stage-data.py" \
  --source "$producer_work/outputs" \
  --destination "$generated_root/instruction-data" \
  --producer-commit "$producer_commit"

python3 "$script_dir/import-transform-data.py" \
  --source "$producer_work/outputs" \
  --destination "$generated_root/transform-data" \
  --producer-commit "$producer_commit" --encodings "$encoding_tsv"

python3 "$script_dir/import-bconv-data.py" \
  --source "$producer_work/outputs" \
  --destination "$generated_root/bconv-data" \
  --producer-commit "$producer_commit" --encodings "$encoding_tsv"

echo '[hputest] inline-asm MM/stage/transform generation/import PASS'
