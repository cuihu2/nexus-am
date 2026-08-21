#!/usr/bin/env bash
set -Eeuo pipefail

script_dir=$(cd "$(dirname "$0")" && pwd)
repo_root=$(cd "$script_dir/.." && pwd)
inline_root="$repo_root/third_party/inline-asm"
asset_build=${HPU_INLINE_ASSET_BUILD:-"$repo_root/build-inline-assets"}
jobs=${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 1)}
lock_dir="$repo_root/build-inline-assets"

if [[ ! -f $inline_root/CMakeLists.txt ]]; then
  printf 'ERROR: missing cuihu2/inline-asm checkout: %s\n' "$inline_root" >&2
  exit 2
fi
if [[ ! $jobs =~ ^[1-9][0-9]*$ ]]; then
  printf 'ERROR: JOBS must be a positive integer: %s\n' "$jobs" >&2
  exit 2
fi
if ! command -v flock >/dev/null 2>&1; then
  printf 'ERROR: flock is required to serialize shared inline-asm outputs\n' >&2
  exit 2
fi

# Every host/sanitizer/Nexus build consumes the same generated outputs tree.
# Serialize the complete configure/generate/validate transaction across build
# directories so one process cannot remove a package while another validates
# it.  Keep the lock under an already ignored build directory, outside the
# generator-owned outputs tree whose contents may be replaced.
mkdir -p "$lock_dir"
exec 9>"$lock_dir/.hpu-delivery.lock"
flock 9

# Publication commands also export the RISC-V compiler support paths.  The
# generator is a host tool, so do not let those cross-toolchain variables leak
# into CMake's native C++ compiler invocation.
env -u GCC_EXEC_PREFIX -u LD_LIBRARY_PATH \
  cmake -S "$inline_root" -B "$asset_build"
env -u GCC_EXEC_PREFIX -u LD_LIBRARY_PATH \
  cmake --build "$asset_build" -j"$jobs" --target hpu_delivery

hardware_root="$inline_root/outputs/ciphertext_multiply/test_data/hardware"
master_image="$hardware_root/hpu_mem_image.u32.bin"
report="$inline_root/outputs/DELIVERY_REPORT.txt"
if [[ ! -f $report ]] || ! grep -qx 'SOFTWARE_DELIVERY=PASS' "$report"; then
  printf 'ERROR: inline-asm delivery did not report SOFTWARE_DELIVERY=PASS\n' >&2
  exit 2
fi

mapfile -t vector_assets < <(
  find "$hardware_root" -type f \( \
    -path '*/images/input/ct_a_q.u32.bin' -o \
    -path '*/images/expected/inputs_ntt_q.u32.bin' -o \
    -path '*/constants/mod_ctx.u32.bin' -o \
    -path '*/constants/twiddle/ntt/basis_0[0-3]/*.u32.bin' -o \
    -path '*/constants/twiddle/intt/basis_0[0-3]/*.u32.bin' \
  \) -print | LC_ALL=C sort
)
if [[ ${#vector_assets[@]} -ne 107 ]]; then
  printf 'ERROR: expected 107 Q4 NTT/INTT vector assets, found %d\n' \
    "${#vector_assets[@]}" >&2
  exit 2
fi
for asset in "${vector_assets[@]}"; do
  if [[ ! -s $asset ]]; then
    printf 'ERROR: empty inline-asm vector asset: %s\n' "$asset" >&2
    exit 2
  fi
done

if [[ ! -f $master_image || $(stat -c '%s' "$master_image") -ne 4342016 ]]; then
  printf 'ERROR: ciphertext master image must be exactly 4342016 bytes: %s\n' \
    "$master_image" >&2
  exit 2
fi
master_sha=$(sha256sum "$master_image")
master_sha=${master_sha%% *}
if [[ $master_sha != fd161793648790ebb02308df49fdd90b0cea02901020b6668124af1e52ca2ee4 ]]; then
  printf 'ERROR: ciphertext master image SHA-256 mismatch: %s\n' \
    "$master_sha" >&2
  exit 2
fi

for operator in encode rescale; do
  operator_image="$inline_root/outputs/$operator/test_data/hardware/hpu_mem_image.u32.bin"
  case "$operator" in
    encode)
      expected_bytes=1048832
      expected_sha=e962d5ef02a4bd8c5b89d2ded1efd8d2410c4582bf41bed235c38bdaa861e408
      ;;
    rescale)
      expected_bytes=1327360
      expected_sha=58e7d67eebcde9a4fd32590a098047fd56fcbd4c71db9df2e467c78f8512dc0d
      ;;
  esac
  if [[ ! -f $operator_image || $(stat -c '%s' "$operator_image") -ne $expected_bytes ]]; then
    printf 'ERROR: %s HPU image size mismatch: %s\n' \
      "$operator" "$operator_image" >&2
    exit 2
  fi
  operator_sha=$(sha256sum "$operator_image")
  operator_sha=${operator_sha%% *}
  if [[ $operator_sha != "$expected_sha" ]]; then
    printf 'ERROR: %s HPU image SHA-256 mismatch: %s\n' \
      "$operator" "$operator_sha" >&2
    exit 2
  fi
done

printf '[inline-asm] validated %d Q4 NTT/INTT assets, Encode/Rescale images, and 16961-line FHE master at %s\n' \
  "${#vector_assets[@]}" "$hardware_root"
