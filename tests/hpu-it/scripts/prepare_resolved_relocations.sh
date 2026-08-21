#!/usr/bin/env bash
set -Eeuo pipefail

script_dir=$(cd "$(dirname "$0")" && pwd)
repo_root=$(cd "$script_dir/.." && pwd)
build_dir=${HPU_IT_HOST_BUILD_DIR:-"$repo_root/build-host"}
jobs=${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 1)}

env -u GCC_EXEC_PREFIX -u LD_LIBRARY_PATH \
  cmake -S "$repo_root" -B "$build_dir" -DHPU_ENABLE_INLINE_ASM=OFF
env -u GCC_EXEC_PREFIX -u LD_LIBRARY_PATH \
  cmake --build "$build_dir" -j"$jobs" --target hpu_it_resolved_relocations
printf '%s\n' "$build_dir/resolved-relocations"
