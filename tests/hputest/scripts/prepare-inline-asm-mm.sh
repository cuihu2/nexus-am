#!/usr/bin/env bash
set -Eeuo pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
test_root=$(cd -- "$script_dir/.." && pwd)
inline_asm_root=${1:-"$test_root/third_party/inline-asm"}
output_root=${2:-"$test_root/build"}
jobs=${3:-${JOBS:-4}}
hpu_seal_root=${4:-"$test_root/third_party/hpu-seal"}
hpu_applications_root=${5:-"$test_root/third_party/hpu-applications"}
poseidon_root=${6:-"$test_root/third_party/poseidon"}

if [[ $inline_asm_root != /* ]]; then
  inline_asm_root=$(cd -- "$test_root" && realpath -m -- "$inline_asm_root")
fi
if [[ $output_root != /* ]]; then
  output_root=$(cd -- "$test_root" && realpath -m -- "$output_root")
fi
if [[ $hpu_seal_root != /* ]]; then
  hpu_seal_root=$(cd -- "$test_root" && realpath -m -- "$hpu_seal_root")
fi
if [[ $hpu_applications_root != /* ]]; then
  hpu_applications_root=$(cd -- "$test_root" && realpath -m -- "$hpu_applications_root")
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
if [[ ! -f $hpu_applications_root/CMakeLists.txt ]] || \
   ! git -C "$hpu_applications_root" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
  echo 'ERROR: HPU applications submodule is not initialized' >&2
  echo 'run: git submodule update --init --recursive tests/hputest/third_party/hpu-applications' >&2
  exit 2
fi
if ! git -C "$hpu_applications_root" diff --ignore-space-at-eol --quiet -- || \
   ! git -C "$hpu_applications_root" diff --cached --quiet --; then
  echo 'ERROR: HPU applications submodule contains tracked modifications' >&2
  exit 2
fi
hpu_applications_path=${hpu_applications_root#"$repository_root/"}
hpu_applications_gitlink=$(git -C "$repository_root" ls-files -s -- "$hpu_applications_path" |
  awk '$1 == "160000" { print $2 }')
hpu_applications_commit=$(git -C "$hpu_applications_root" rev-parse HEAD)
if [[ -z $hpu_applications_gitlink || \
      $hpu_applications_commit != "$hpu_applications_gitlink" ]]; then
  echo 'ERROR: HPU applications checkout does not match the Nexus-AM gitlink' >&2
  echo "gitlink:  $hpu_applications_gitlink" >&2
  echo "checkout: $hpu_applications_commit" >&2
  exit 2
fi
for tool in cmake python3 "${CXX:-c++}"; do
  command -v "$tool" >/dev/null || {
    echo "ERROR: missing build tool: $tool" >&2
    exit 2
  }
done

poseidon_root=$(realpath -m -- "$poseidon_root")
poseidon_path=${poseidon_root#"$repository_root/"}
poseidon_gitlink=$(git -C "$repository_root" ls-files -s -- "$poseidon_path" |
  awk '$1 == "160000" { print $2 }')
poseidon_commit=$(git -C "$poseidon_root" rev-parse HEAD)
if [[ -z $poseidon_gitlink || $poseidon_commit != "$poseidon_gitlink" ]] ||
   ! git -C "$poseidon_root" diff --quiet -- ||
   ! git -C "$poseidon_root" diff --cached --quiet --; then
  echo 'ERROR: Poseidon must be clean and match the Nexus-AM gitlink' >&2
  exit 2
fi
poseidon_build="$output_root/poseidon-cmake/$poseidon_commit"
cmake -S "$poseidon_root" -B "$poseidon_build" \
  -DCMAKE_BUILD_TYPE=Release -DPOSEIDON_USE_HARDWARE=OFF \
  -DPOSEIDON_BUILD_EXAMPLES=OFF -DPOSEIDON_USE_ZLIB=OFF -DPOSEIDON_USE_ZSTD=OFF \
  -DPOSEIDON_USE_MSGSL=OFF -DPOSEIDON_USE_INTEL_HEXL=OFF -DPOSEIDON_USE_SPDLOG=OFF
cmake --build "$poseidon_build" --parallel "$jobs" --target poseidon_shared

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
hpu_rescale_build="$output_root/hpu-seal-cmb013-cmake/$hpu_seal_commit"
hpu_rescale_source="$output_root/hpu-seal-producer/$hpu_seal_commit/ckks/rescale"
hpu_seal_rotate_build="$output_root/hpu-seal-cmb014-cmake/$hpu_seal_commit"
hpu_seal_rotate_source="$output_root/hpu-seal-producer/$hpu_seal_commit/rotate"
hpu_seal_rotate_import="$generated_root/rotate-data"
hpu_seal_tool_root="$output_root/hpu-seal-tools/$hpu_seal_commit"
hpu_seal_encodings="$hpu_seal_tool_root/hadd_encoder_words.tsv"
hmul_source="$output_root/hpu-seal-producer/$hpu_seal_commit/hmul"
hmul_import="$generated_root/hmul-data"
hmul_encodings="$hpu_seal_tool_root/hmul_encoder_words.tsv"
hpu_seal_encoder="$hpu_seal_tool_root/verify-ckks-encoding"
hpu_applications_build="$output_root/app006-applications-cmake/$hpu_applications_commit"
hpu_applications_posix_root=${HPU_APPLICATION_POSIX_ROOT:-${XDG_CACHE_HOME:-"$HOME/.cache"}/nexus-am-hputest/hpu-applications/$hpu_applications_commit/outputs}

mkdir -p -- "$cmake_build" "$tool_root" "$generated_root" "$producer_work" \
  "$hpu_seal_build" "$hpu_seal_source" "$hmul_source" "$hpu_reline_build" \
  "$hpu_reline_source" "$hpu_rescale_build" "$hpu_rescale_source" \
  "$hpu_seal_rotate_build" \
  "$hpu_seal_rotate_source" "$hpu_seal_tool_root"
mkdir -p -- "$hpu_applications_build" "$hpu_applications_posix_root"
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

echo "[hputest] generating main v1 CKKS/BFV/BGV packages at $hpu_applications_commit"
hpu_main_build="$output_root/hpu-main-cmake/$hpu_applications_commit"
package_parent="$output_root/hpu-main-producer/$hpu_applications_commit"
mkdir -p "$package_parent"
# 本次独立生成，普通/静默编译复用同一份包，不能混用跨次随机密钥。
package_root=$(mktemp -d "$package_parent/generation.XXXXXX")
cmake -S "$test_root/tools/hpu-scheme-cases" -B "$hpu_main_build" \
  -DINLINE_ASM_ROOT="$hpu_applications_root" -DCMAKE_BUILD_TYPE=Release \
  -DPOSEIDON_ROOT="$poseidon_root" -DPOSEIDON_BUILD="$poseidon_build" \
  -DHPU_APPLICATION_OUTPUT_ROOT="$package_root"
HPU_DELIVERY_COMMIT="$hpu_applications_commit" \
HPU_DELIVERY_WORKTREE_STATE=clean-at-configure \
  cmake --build "$hpu_main_build" --parallel "$jobs" --target \
    hpu_fhe_delivery hpu_scheme_case_generator hpu_poseidon_case_generator \
    hpu_program_model hpu_program_model_test hpu_encode_program
"$hpu_main_build/hpu_program_model_test"
while IFS=$'\t' read -r case_id stem scheme degree role source; do
  [[ $case_id == case_id ]] && continue
  if [[ $role != application ]]; then
    generator="$hpu_main_build/hpu_scheme_case_generator"
    if [[ $stem == poseidon_* ]]; then generator="$hpu_main_build/hpu_poseidon_case_generator"; fi
    HPU_DELIVERY_COMMIT="$hpu_applications_commit" \
    HPU_DELIVERY_WORKTREE_STATE=clean-at-configure \
      "$generator" "$scheme" "$role" "$degree" \
        "$package_root/$stem"
  fi
  python3 "$script_dir/import-application-package.py" \
    --source "$package_root/$stem" \
    --destination "$generated_root/application-data/$stem" \
    --validator "$hpu_main_build/inline-asm/hpu_validate_package" \
    --encoder "$hpu_main_build/hpu_encode_program" \
    --program-model "$hpu_main_build/hpu_program_model" \
    --producer-commit "$hpu_applications_commit" --poseidon-commit "$poseidon_commit"
done < "$test_root/scheme-cases.tsv"
printf '%s\n' "$package_root" > "$generated_root/application-data/PRODUCER_ROOT"
printf '%s\n' "$hpu_applications_commit" > "$generated_root/application-data/PRODUCER_COMMIT"
printf '%s\n' "$poseidon_commit" > "$generated_root/application-data/POSEIDON_COMMIT"
echo '[hputest] legacy primitive/control + main scheme/application import PASS'
