#!/usr/bin/env bash
set -Eeuo pipefail

script_dir=$(cd "$(dirname "$0")" && pwd)
repo_root=$(cd "$script_dir/.." && pwd)
image_path=${1:-"$repo_root/build/images/latest"}
release_root=${2:-"$repo_root/build/release"}

for tool in awk basename cp find git grep mkdir mktemp mv rmdir sha256sum \
            sort tar unlink xargs; do
  command -v "$tool" >/dev/null || {
    printf 'ERROR: missing %s\n' "$tool" >&2
    exit 2
  }
done
for tool in readelf strings; do
  command -v "riscv64-linux-gnu-$tool" >/dev/null || {
    printf 'ERROR: missing riscv64-linux-gnu-%s\n' "$tool" >&2
    exit 2
  }
done

if [[ ! -d $image_path ]]; then
  printf 'ERROR: validated image directory does not exist: %s\n' \
    "$image_path" >&2
  exit 2
fi
image_dir=$(cd "$image_path" && pwd -P)

# Package only a complete, current 49-case publication.  The release subset is
# selected after this gate, so a partial or stale build cannot become an IT
# release asset.
"$script_dir/validate_artifacts.sh" "$image_dir"

for metadata in .am-revision .hpu-mem-profile .source-fingerprint \
                .toolchain-identity .qualification-status.csv; do
  if [[ ! -s $image_dir/$metadata ]]; then
    printf 'ERROR: missing release metadata %s in %s\n' \
      "$metadata" "$image_dir" >&2
    exit 2
  fi
done

mem_profile=$(<"$image_dir/.hpu-mem-profile")
profile_re='^HPU_MEM_BASE=(0[xX][0-9a-fA-F]+|[0-9]+);HPU_MEM_LINES=([1-9][0-9]*);HPU_LINE_BYTES=256;HPU_MEM_BYTES=([1-9][0-9]*)$'
if [[ ! $mem_profile =~ $profile_re ]]; then
  printf 'ERROR: malformed HPU memory profile: %s\n' "$mem_profile" >&2
  exit 2
fi
hpu_mem_base=${BASH_REMATCH[1]}
hpu_mem_lines=${BASH_REMATCH[2]}
source_fingerprint=$(<"$image_dir/.source-fingerprint")
if [[ ! $source_fingerprint =~ ^[0-9a-f]{16}$ ]]; then
  printf 'ERROR: malformed source fingerprint: %s\n' \
    "$source_fingerprint" >&2
  exit 2
fi
am_revision=$(<"$image_dir/.am-revision")
source_date_epoch=$(git -C "$repo_root/../.." show -s --format=%ct \
  "$am_revision" 2>/dev/null) || {
    printf 'ERROR: cannot resolve build revision timestamp: %s\n' \
      "$am_revision" >&2
    exit 2
  }
if [[ ! $source_date_epoch =~ ^[1-9][0-9]*$ ]]; then
  printf 'ERROR: invalid build revision timestamp: %s\n' \
    "$source_date_epoch" >&2
  exit 2
fi

package_name="hpu-compute-composite-performance-${hpu_mem_base}-${hpu_mem_lines}-${source_fingerprint}"
mkdir -p "$release_root"
release_root=$(cd "$release_root" && pwd -P)
package_dir="$release_root/$package_name"
archive="$release_root/$package_name.tar.gz"
archive_checksum="$archive.sha256"
if [[ -e $package_dir || -L $package_dir || -e $archive || \
      -e $archive_checksum ]]; then
  printf 'ERROR: immutable release output already exists for %s\n' \
    "$package_name" >&2
  exit 2
fi

staging_root=$(mktemp -d "$release_root/.staging-release.XXXXXXXX")
archive_tmp="$release_root/.$package_name.tar.gz.tmp.$$"
checksum_tmp="$release_root/.$package_name.tar.gz.sha256.tmp.$$"
cleanup() {
  if [[ -d ${staging_root:-} ]]; then
    find "$staging_root" -mindepth 1 -delete
    rmdir "$staging_root"
  fi
  if [[ -f ${archive_tmp:-} ]]; then
    unlink "$archive_tmp"
  fi
  if [[ -f ${checksum_tmp:-} ]]; then
    unlink "$checksum_tmp"
  fi
}
trap cleanup EXIT
trap 'exit 130' HUP INT TERM

staging_package="$staging_root/$package_name"
mkdir "$staging_package"

mapfile -t delivery_sources < <(
  for category in 03_compute_instructions \
                  04_composite_instruction_sequences \
                  06_performance; do
    find "$repo_root/$category" -type f -name 'HPU_IT_*.c' \
      ! -name 'HPU_IT_DIR_CMB_015.c' -printf "$category/%P\n"
  done | LC_ALL=C sort
)
if [[ ${#delivery_sources[@]} -ne 27 ]]; then
  printf 'ERROR: expected 27 release testcase sources, found %d\n' \
    "${#delivery_sources[@]}" >&2
  exit 2
fi

printf 'category,case_id,source\n' \
  > "$staging_package/.delivery-cases.csv"
printf 'case_id,delivery_status,reason\n' \
  > "$staging_package/.qualification-status.csv"

dma_manifest_count=0
for relative_source in "${delivery_sources[@]}"; do
  case "$relative_source" in
    03_compute_instructions/*) category=03_compute_instructions ;;
    04_composite_instruction_sequences/*)
      category=04_composite_instruction_sequences ;;
    06_performance/*) category=06_performance ;;
    *)
      printf 'ERROR: unexpected release testcase path: %s\n' \
        "$relative_source" >&2
      exit 2 ;;
  esac
  case_id=$(basename "$relative_source" .c)
  if [[ $case_id == HPU_IT_DIR_CMB_015 ]]; then
    printf 'ERROR: blocked testcase reached release subset\n' >&2
    exit 2
  fi

  status_line=$(awk -F, -v case_id="$case_id" \
    '$1 == case_id { print; count++ } END { if (count != 1) exit 2 }' \
    "$image_dir/.qualification-status.csv") || {
      printf 'ERROR: missing or duplicate qualification status for %s\n' \
        "$case_id" >&2
      exit 2
    }
  expected_status="$case_id,EXECUTABLE_PASS_READY,output_check_authoritative"
  if [[ $status_line != "$expected_status" ]]; then
    printf 'ERROR: testcase is not PASS-ready: %s\n' "$status_line" >&2
    exit 2
  fi

  for suffix in elf bin txt; do
    source_artifact="$image_dir/$case_id.$suffix"
    if [[ ! -s $source_artifact ]]; then
      printf 'ERROR: missing %s release artifact for %s\n' \
        "$suffix" "$case_id" >&2
      exit 2
    fi
    cp "$source_artifact" "$staging_package/"
  done
  if [[ -s $image_dir/$case_id.dma.csv ]]; then
    cp "$image_dir/$case_id.dma.csv" "$staging_package/"
    dma_manifest_count=$((dma_manifest_count + 1))
  fi

  printf '%s,%s,%s\n' "$category" "$case_id" "$relative_source" \
    >> "$staging_package/.delivery-cases.csv"
  printf '%s\n' "$status_line" \
    >> "$staging_package/.qualification-status.csv"
done

if [[ $dma_manifest_count -ne 11 ]]; then
  printf 'ERROR: expected 11 DMA manifests in release subset, found %d\n' \
    "$dma_manifest_count" >&2
  exit 2
fi

for metadata in .am-revision .hpu-mem-profile .source-fingerprint \
                .toolchain-identity; do
  cp "$image_dir/$metadata" "$staging_package/$metadata"
done

for elf in "$staging_package"/*.elf; do
  header=$(riscv64-linux-gnu-readelf -h "$elf")
  grep -Fq 'Machine:                           RISC-V' <<<"$header" || {
    printf 'ERROR: release package contains non-RISC-V ELF: %s\n' \
      "$elf" >&2
    exit 2
  }
  strings_output=$(riscv64-linux-gnu-strings -a "$elf")
  if grep -Fxq PASS_PROBE <<<"$strings_output"; then
    printf 'ERROR: release package contains obsolete PASS_PROBE marker: %s\n' \
      "$elf" >&2
    exit 2
  fi
done

(
  cd "$staging_package"
  find . -maxdepth 1 -type f ! -name SHA256SUMS -printf '%f\0' | \
    LC_ALL=C sort -z | xargs -0 sha256sum > SHA256SUMS
  sha256sum -c SHA256SUMS >/dev/null
)

tar --sort=name --mtime="@$source_date_epoch" --owner=0 --group=0 \
  --numeric-owner -C "$staging_root" -czf "$archive_tmp" "$package_name"
archive_digest=$(sha256sum "$archive_tmp")
printf '%s  %s\n' "${archive_digest%% *}" "${archive##*/}" \
  > "$checksum_tmp"

mv -T "$staging_package" "$package_dir"
rmdir "$staging_root"
staging_root=
mv "$archive_tmp" "$archive"
archive_tmp=
mv "$checksum_tmp" "$archive_checksum"
checksum_tmp=
(cd "$release_root" && sha256sum -c "${archive_checksum##*/}" >/dev/null)

trap - EXIT HUP INT TERM
printf 'HPU_IT_RELEASE_PACKAGE=%s\n' "$package_dir"
printf 'HPU_IT_RELEASE_ARCHIVE=%s\n' "$archive"
printf 'HPU_IT_RELEASE_CHECKSUM=%s\n' "$archive_checksum"
