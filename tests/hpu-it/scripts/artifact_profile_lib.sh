#!/usr/bin/env bash

# Shared by the publisher and validator.  Keep the input list and digest
# algorithm in one place so an artifact directory can always be reproduced
# from its declared build inputs.
hpu_case_sources() {
  local repo_root=$1
  find "$repo_root" -mindepth 3 -maxdepth 3 -type f -name 'HPU_IT_*.c' \
    -printf '%P\n' | LC_ALL=C sort
}

hpu_generated_operator_sources() {
  local repo_root=$1
  find "$repo_root/08_generated_inline_asm_operators" -maxdepth 1 -type f \
    -name 'HPU_IT_GEN_*.c' -printf '%P\n' | \
    sed 's#^#08_generated_inline_asm_operators/#' | LC_ALL=C sort
}

hpu_fingerprint_inputs() {
  local repo_root=$1
  printf '%s\n' \
    CMakeLists.txt \
    POSEIDON_REMAINING_CASES_AUDIT.md \
    README.md \
    THIRD_PARTY_NOTICES.md \
    hpu_inline_asm.h \
    nexus-am.mk \
    runtime/hpu_fhe.c \
    runtime/hpu_fhe.h \
    runtime/hpu_generated_ops.c \
    runtime/hpu_generated_ops.h \
    runtime/hpu_relocation_dump.c \
    runtime/hpu_test.c \
    runtime/hpu_test.h \
    runtime/hpu_test_main.c \
    runtime/hpu_vectors.c \
    runtime/hpu_vectors.h \
    scripts/artifact_profile_lib.sh \
    scripts/build_nexus_am.sh \
    scripts/build_generated_operators.sh \
    scripts/prepare_inline_vectors.sh \
    scripts/prepare_resolved_relocations.sh \
    scripts/validate_artifacts.sh \
    scripts/validate_generated_operators.sh
  # The generated instruction programs are executable build inputs, not merely
  # documentation.  Hash both their generator/encoder sources and the exact
  # emitted streams/manifests consumed by Nexus-AM.
  find "$repo_root/third_party/inline-asm" -type f \( \
      -path '*/include/*.hpp' -o \
      -path '*/include/*/*.hpp' -o \
      -path '*/src/*.cpp' -o \
      -path '*/src/*/*.cpp' -o \
      -path '*/encode/include/*.hpp' -o \
      -path '*/encode/src/*.cpp' -o \
      -path '*/test/encode/*.cpp' -o \
      -name 'CMakeLists.txt' \
    \) -printf '%P\n' | sed 's#^#third_party/inline-asm/#' | LC_ALL=C sort
  find "$repo_root/third_party/inline-asm/outputs" -mindepth 2 -maxdepth 2 \
    -type f \( -name '*.cpp' -o -name '*.h' -o -name '*.asm' -o \
                 -name '*.inst32' -o -name '*.cmd26' -o \
                 -name 'dma_relocation_manifest.csv' \) \
    -printf '%P\n' | sed 's#^#third_party/inline-asm/outputs/#' | LC_ALL=C sort
  find "$repo_root/third_party/inline-asm/outputs/ciphertext_multiply/test_data/hardware" \
    -type f \( \
      -path '*/images/input/ct_a_q.u32.bin' -o \
      -path '*/images/expected/inputs_ntt_q.u32.bin' -o \
      -path '*/constants/mod_ctx.u32.bin' -o \
      -path '*/constants/twiddle/ntt/basis_0[0-3]/*.u32.bin' -o \
      -path '*/constants/twiddle/intt/basis_0[0-3]/*.u32.bin' \
    \) -printf '%P\n' | sed \
      's#^#third_party/inline-asm/outputs/ciphertext_multiply/test_data/hardware/#' | \
    LC_ALL=C sort
  printf '%s\n' \
    third_party/inline-asm/outputs/ciphertext_multiply/test_data/hardware/hpu_mem_image.u32.bin
  for operator in pmult cmult modup moddown auto encode rescale; do
    printf '%s\n' \
      "third_party/inline-asm/outputs/$operator/test_data/hardware/hpu_mem_image.u32.bin"
  done
  hpu_case_sources "$repo_root"
  hpu_generated_operator_sources "$repo_root"
}

hpu_compute_source_fingerprint() {
  local repo_root=$1
  local hpu_mem_base=$2
  local hpu_mem_lines=$3
  local am_revision=$4
  local compiler_identity=$5
  shift 5
  local input input_digest full_digest

  full_digest=$(
    {
      printf 'HPU_IT_MEM_BASE=%s\n' "$hpu_mem_base"
      printf 'HPU_IT_MEM_LINES=%s\n' "$hpu_mem_lines"
      printf 'AM_REVISION=%s\n' "$am_revision"
      printf 'TOOLCHAIN=%s\n' "$compiler_identity"
      for input in "$@"; do
        input_digest=$(sha256sum "$repo_root/$input")
        printf '%s  %s\n' "${input_digest%% *}" "$input"
      done
    } | sha256sum
  )
  full_digest=${full_digest%% *}
  printf '%s\n' "${full_digest:0:16}"
}
