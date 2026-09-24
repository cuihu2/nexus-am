#!/usr/bin/env bash
set -Eeuo pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
test_root=$(cd -- "$script_dir/.." && pwd)
inline_asm_root=${1:-"$test_root/third_party/inline-asm"}
output_root=${2:-"$test_root/build"}
jobs=${3:-${JOBS:-4}}
hpu_seal_root=${4:-"$test_root/third_party/hpu-seal"}

if [[ $inline_asm_root != /* ]]; then
  inline_asm_root=$(cd -- "$test_root" && realpath -m -- "$inline_asm_root")
fi
if [[ $output_root != /* ]]; then
  output_root=$(cd -- "$test_root" && realpath -m -- "$output_root")
fi
if [[ $hpu_seal_root != /* ]]; then
  hpu_seal_root=$(cd -- "$test_root" && realpath -m -- "$hpu_seal_root")
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
if ! git -C "$inline_asm_root" diff --ignore-space-at-eol --quiet -- || \
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
if [[ ! -f $hpu_seal_root/CMakeLists.txt ]] || \
   ! git -C "$hpu_seal_root" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
  echo 'ERROR: HPU_SEAL submodule is not initialized' >&2
  echo 'run: git submodule update --init --recursive tests/hputest/third_party/hpu-seal' >&2
  exit 2
fi
if ! git -C "$hpu_seal_root" diff --ignore-space-at-eol --quiet -- || \
   ! git -C "$hpu_seal_root" diff --cached --quiet --; then
  echo 'ERROR: HPU_SEAL submodule contains tracked modifications' >&2
  exit 2
fi
hpu_seal_path=${hpu_seal_root#"$repository_root/"}
hpu_seal_gitlink=$(git -C "$repository_root" ls-files -s -- "$hpu_seal_path" |
  awk '$1 == "160000" { print $2 }')
hpu_seal_commit=$(git -C "$hpu_seal_root" rev-parse HEAD)
if [[ -z $hpu_seal_gitlink || $hpu_seal_commit != "$hpu_seal_gitlink" ]]; then
  echo 'ERROR: HPU_SEAL checkout does not match the Nexus-AM gitlink' >&2
  echo "gitlink:  $hpu_seal_gitlink" >&2
  echo "checkout: $hpu_seal_commit" >&2
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
producer_keyswitch="$producer_work/outputs/keyswitch"
generated_root="$output_root/generated"
import_root="$generated_root/inline-asm/mm"
keyswitch_source_root="$generated_root/keyswitch-source"
keyswitch_import_root="$generated_root/keyswitch-data"
auto_import_root="$generated_root/auto-data"
generated_header="$generated_root/include/hpu/inline_asm_mm_delivery.h"
tool_root="$output_root/inline-asm-tools"
encoding_tsv="$tool_root/encoder_words.tsv"
hpu_seal_build="$output_root/hpu-seal-operators-cmake/$hpu_seal_commit"
hpu_seal_source="$output_root/hpu-seal-producer/$hpu_seal_commit/hadd"
hpu_seal_import="$generated_root/hadd-data"
hpu_reline_build="$output_root/hpu-seal-cmb012-cmake/$hpu_seal_commit"
hpu_reline_source="$output_root/hpu-seal-producer/$hpu_seal_commit/ckks/reline"
hpu_seal_rotate_build="$output_root/hpu-seal-cmb014-cmake/$hpu_seal_commit"
hpu_seal_rotate_source="$output_root/hpu-seal-producer/$hpu_seal_commit/rotate"
hpu_seal_rotate_import="$generated_root/rotate-data"
hpu_seal_tool_root="$output_root/hpu-seal-tools/$hpu_seal_commit"
hpu_seal_encodings="$hpu_seal_tool_root/hadd_encoder_words.tsv"
hmul_source="$output_root/hpu-seal-producer/$hpu_seal_commit/hmul"
hmul_import="$generated_root/hmul-data"
hmul_encodings="$hpu_seal_tool_root/hmul_encoder_words.tsv"
hpu_seal_encoder="$hpu_seal_tool_root/verify-ckks-encoding"

mkdir -p -- "$cmake_build" "$tool_root" "$generated_root" "$producer_work" \
  "$hpu_seal_build" "$hpu_seal_source" "$hmul_source" "$hpu_reline_build" \
  "$hpu_reline_source" "$hpu_seal_rotate_build" \
  "$hpu_seal_rotate_source" "$hpu_seal_tool_root"
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

echo "[hputest] generating selected inline-asm MM and KeySwitch inputs at $producer_commit"
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

python3 "$script_dir/stage-keyswitch-package.py" \
  --source "$producer_keyswitch" \
  --destination "$keyswitch_source_root" \
  --producer-commit "$producer_commit"

python3 "$script_dir/import-keyswitch-data.py" \
  --source "$producer_work/outputs" \
  --destination "$keyswitch_import_root" \
  --producer-commit "$producer_commit" \
  --encodings "$encoding_tsv"

python3 "$script_dir/import-auto-data.py" \
  --source "$producer_work/outputs" \
  --destination "$auto_import_root" \
  --producer-commit "$producer_commit" \
  --encodings "$encoding_tsv"

echo "[hputest] generating HPU_SEAL BFV HADD at $hpu_seal_commit"
cmake -S "$test_root/tools/hpu-seal-operators" -B "$hpu_seal_build" \
  -DINLINE_ASM_ROOT="$hpu_seal_root" \
  -DCMAKE_BUILD_TYPE=Release
cmake --build "$hpu_seal_build" --parallel "$jobs" --target \
  hpu_seal_cmb009_generator hpu_seal_cmb010_generator
"$hpu_seal_build/hpu_seal_cmb009_generator" "$hpu_seal_source" "$hpu_seal_commit"

"${CXX:-c++}" \
  -std=c++17 -Wall -Wextra -Werror \
  -I"$hpu_seal_root/encode/include" \
  "$script_dir/generate-hpu-seal-hadd-encodings.cpp" \
  "$hpu_seal_root/encode/src/instruction.cpp" \
  "$hpu_seal_root/encode/src/parser.cpp" \
  "$hpu_seal_root/encode/src/encoder.cpp" \
  "$hpu_seal_root/encode/src/assembler.cpp" \
  -o "$hpu_seal_tool_root/generate-hpu-seal-hadd-encodings"
"$hpu_seal_tool_root/generate-hpu-seal-hadd-encodings" "$hpu_seal_encodings"

python3 "$script_dir/import-hpu-seal-hadd.py" \
  --source "$hpu_seal_source" \
  --destination "$hpu_seal_import" \
  --producer-commit "$hpu_seal_commit" \
  --encodings "$hpu_seal_encodings"

echo "[hputest] generating HPU_SEAL BFV HMUL at $hpu_seal_commit"
"$hpu_seal_build/hpu_seal_cmb010_generator" "$hmul_source" "$hpu_seal_commit"

"${CXX:-c++}" \
  -std=c++17 -Wall -Wextra -Werror \
  -I"$hpu_seal_root/encode/include" \
  "$script_dir/generate-hpu-seal-hmul-encodings.cpp" \
  "$hpu_seal_root/encode/src/instruction.cpp" \
  "$hpu_seal_root/encode/src/parser.cpp" \
  "$hpu_seal_root/encode/src/encoder.cpp" \
  "$hpu_seal_root/encode/src/assembler.cpp" \
  -o "$hpu_seal_tool_root/generate-hpu-seal-hmul-encodings"
"$hpu_seal_tool_root/generate-hpu-seal-hmul-encodings" \
  "$hmul_source/hmul.asm" "$hmul_encodings"

python3 "$script_dir/import-hpu-seal-hmul.py" \
  --source "$hmul_source" \
  --destination "$hmul_import" \
  --producer-commit "$hpu_seal_commit" \
  --encodings "$hmul_encodings"

echo "[hputest] generating HPU_SEAL CKKS Reline at $hpu_seal_commit"
cmake -S "$test_root/tools/hpu-seal-cmb012" -B "$hpu_reline_build" \
  -DINLINE_ASM_ROOT="$hpu_seal_root" \
  -DCMAKE_BUILD_TYPE=Release
cmake --build "$hpu_reline_build" --parallel "$jobs" \
  --target hpu_seal_cmb012_generator
"$hpu_reline_build/hpu_seal_cmb012_generator" \
  "$hpu_reline_source" "$hpu_seal_commit" \
  > "$hpu_reline_source/HOST_ORACLE.log"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
  -I"$hpu_seal_root/encode/include" "$script_dir/verify-ckks-encoding.cpp" \
  "$hpu_seal_root/encode/src/instruction.cpp" \
  "$hpu_seal_root/encode/src/parser.cpp" \
  "$hpu_seal_root/encode/src/encoder.cpp" \
  "$hpu_seal_root/encode/src/assembler.cpp" \
  -o "$hpu_seal_encoder"
python3 "$script_dir/import-ckks-data.py" --source "$hpu_reline_source" \
  --destination "$generated_root/ckks-data/reline" --profile reline \
  --encoder "$hpu_seal_encoder" \
  --producer-commit "$hpu_seal_commit"

echo "[hputest] generating HPU_SEAL BFV RotateRows at $hpu_seal_commit"
cmake -S "$test_root/tools/hpu-seal-cmb014" -B "$hpu_seal_rotate_build" \
  -DINLINE_ASM_ROOT="$hpu_seal_root" \
  -DCMAKE_BUILD_TYPE=Release
cmake --build "$hpu_seal_rotate_build" --parallel "$jobs" \
  --target hpu_seal_cmb014_generator
"$hpu_seal_rotate_build/hpu_seal_cmb014_generator" \
  "$hpu_seal_rotate_source" "$hpu_seal_commit" \
  > "$hpu_seal_rotate_source/HOST_ORACLE.log"

python3 "$script_dir/import-hpu-seal-rotate.py" \
  --source "$hpu_seal_rotate_source" \
  --destination "$hpu_seal_rotate_import" \
  --producer-commit "$hpu_seal_commit" \
  --encoder "$hpu_seal_encoder"

echo "[hputest] adding upstream CKKS applications at $hpu_seal_commit"
cmake --build "$hpu_seal_build" --parallel "$jobs" --target \
  hpu_ckks_polynomial_example hpu_ckks_composed_application_example
for profile in polynomial composed; do
  ckks_source="$output_root/hpu-seal-producer/$hpu_seal_commit/ckks/$profile"
  mkdir -p "$ckks_source"
  target=hpu_ckks_polynomial_example
  [[ $profile != composed ]] || target=hpu_ckks_composed_application_example
  # upstream 主机端先通过 SEAL 精确密文/解密误差检查，才导出交付包。
  "$hpu_seal_build/inline-asm/$target" --emit-dir "$ckks_source" \
    > "$ckks_source/HOST_ORACLE.log"
  python3 "$script_dir/import-ckks-data.py" --source "$ckks_source" \
    --destination "$generated_root/ckks-data/$profile" --profile "$profile" \
    --encoder "$hpu_seal_encoder" \
    --producer-commit "$hpu_seal_commit"
done

echo '[hputest] inline-asm and HPU_SEAL HADD/HMUL/Reline/Rotate/CKKS semantic import PASS'
