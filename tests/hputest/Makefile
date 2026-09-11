AM_HOME ?= $(abspath ../..)
ARCH ?= riscv64-xs
CROSS_COMPILE ?= riscv64-linux-gnu-
OUTPUT_ROOT ?= $(CURDIR)/build
JOBS ?= 4
INLINE_ASM_ROOT ?= $(CURDIR)/third_party/inline-asm
HPU_GENERATED_ROOT ?= $(OUTPUT_ROOT)/generated
export mainargs
HPU_DUMP_RESULTS ?= 0
DIAGNOSTIC_OUTPUT_ROOT ?= $(OUTPUT_ROOT)/diagnostic
SUBTEST_OUTPUT_ROOT ?= $(OUTPUT_ROOT)/subtests

.PHONY: all group one diagnostic subtests prepare-inline-asm-mm verify-inline-asm validate clean

all: verify-inline-asm
	@AM_HOME="$(AM_HOME)" ARCH="$(ARCH)" \
	  CROSS_COMPILE="$(CROSS_COMPILE)" JOBS="$(JOBS)" \
	  INLINE_ASM_ROOT="$(INLINE_ASM_ROOT)" \
	  HPU_GENERATED_ROOT="$(HPU_GENERATED_ROOT)" \
	  HPU_DUMP_RESULTS="$(HPU_DUMP_RESULTS)" \
	  scripts/build.sh "$(OUTPUT_ROOT)"

group: verify-inline-asm
	@test -n "$(GROUP)" || { echo 'ERROR: use make group GROUP=core|transform|fhe'; exit 2; }
	@AM_HOME="$(AM_HOME)" ARCH="$(ARCH)" \
	  CROSS_COMPILE="$(CROSS_COMPILE)" JOBS="$(JOBS)" \
	  INLINE_ASM_ROOT="$(INLINE_ASM_ROOT)" \
	  HPU_GENERATED_ROOT="$(HPU_GENERATED_ROOT)" \
	  HPU_DUMP_RESULTS="$(HPU_DUMP_RESULTS)" \
	  HPU_CASE_GROUP="$(GROUP)" \
	  scripts/build.sh "$(OUTPUT_ROOT)"

one: verify-inline-asm
	@test -n "$(CASE)" || { echo 'ERROR: use make one CASE=path/from/src/without/.c'; exit 2; }
	@AM_HOME="$(AM_HOME)" ARCH="$(ARCH)" \
	  CROSS_COMPILE="$(CROSS_COMPILE)" JOBS="$(JOBS)" \
	  INLINE_ASM_ROOT="$(INLINE_ASM_ROOT)" \
	  HPU_GENERATED_ROOT="$(HPU_GENERATED_ROOT)" \
	  HPU_DUMP_RESULTS="$(HPU_DUMP_RESULTS)" \
	  scripts/build.sh "$(OUTPUT_ROOT)" "$(CASE)"

# 先用 all/verify-inline-asm 准备并校验生产者数据，再复用同一份数据编译诊断版。
# 诊断对象和产物独立，不能把上一轮常规对象当成全量 UART 版本发布。
diagnostic:
	@test -s "$(HPU_GENERATED_ROOT)/include/hpu/inline_asm_mm_delivery.h" || \
	  { echo 'ERROR: run make all or make verify-inline-asm before make diagnostic'; exit 2; }
	@AM_HOME="$(AM_HOME)" ARCH="$(ARCH)" \
	  CROSS_COMPILE="$(CROSS_COMPILE)" JOBS="$(JOBS)" \
	  INLINE_ASM_ROOT="$(INLINE_ASM_ROOT)" \
	  HPU_GENERATED_ROOT="$(HPU_GENERATED_ROOT)" \
	  HPU_DUMP_RESULTS=1 HPU_CASE_GROUP=diagnostic \
	  scripts/build.sh "$(DIAGNOSTIC_OUTPUT_ROOT)"

# 不重复生成producer；每个ELF固化一个subcase，编译链接串行以保护共享AM库。
subtests:
	@python3 scripts/build-subtests.py \
	  --output-root "$(SUBTEST_OUTPUT_ROOT)" \
	  --generated-root "$(HPU_GENERATED_ROOT)" \
	  --arch "$(ARCH)" --cross-compile "$(CROSS_COMPILE)" \
	  --jobs "$(JOBS)" --dump-results "$(HPU_DUMP_RESULTS)"

prepare-inline-asm-mm:
	@scripts/prepare-inline-asm-mm.sh \
	  "$(INLINE_ASM_ROOT)" "$(OUTPUT_ROOT)" "$(JOBS)"

verify-inline-asm: prepare-inline-asm-mm
	@HPU_GENERATED_ROOT="$(HPU_GENERATED_ROOT)" \
	  scripts/verify-inline-asm-encodings.sh "$(INLINE_ASM_ROOT)"
	@python3 scripts/test-mm-delivery.py "$(HPU_GENERATED_ROOT)/inline-asm/mm/mm.c"
	@python3 scripts/test-opcode-mapping.py
	@python3 scripts/test-completion.py
	@python3 scripts/test-dma-poll.py
	@python3 scripts/test-fixture-diagnostics.py
	@python3 scripts/test-runtime-wait.py
	@python3 scripts/test-v2-memory.py
	@python3 scripts/test-result-report.py
	@python3 scripts/test-progress.py
	@python3 scripts/test-subtest-support.py
	@python3 scripts/test-run-subtests.py
	@python3 scripts/test-stage-reference.py
	@python3 scripts/test-stage-data.py \
	  --source "$(OUTPUT_ROOT)/inline-asm-producer/$$(git -C "$(INLINE_ASM_ROOT)" rev-parse HEAD)/outputs"
	@python3 scripts/test-mm-layout.py \
	  --source "$(OUTPUT_ROOT)/inline-asm-producer/$$(git -C "$(INLINE_ASM_ROOT)" rev-parse HEAD)/outputs/mm"
	@python3 scripts/test-package-chapters.py
	@python3 scripts/test-transform-delivery.py \
	  --source "$(OUTPUT_ROOT)/inline-asm-producer/$$(git -C "$(INLINE_ASM_ROOT)" rev-parse HEAD)/outputs" \
	  --encodings "$(OUTPUT_ROOT)/inline-asm-tools/encoder_words.tsv"
	@python3 scripts/test-bconv-delivery.py \
	  --source "$(OUTPUT_ROOT)/inline-asm-producer/$$(git -C "$(INLINE_ASM_ROOT)" rev-parse HEAD)/outputs" \
	  --encodings "$(OUTPUT_ROOT)/inline-asm-tools/encoder_words.tsv"

validate:
	@HPU_GENERATED_ROOT="$(HPU_GENERATED_ROOT)" \
	  scripts/validate-build.sh "$(OUTPUT_ROOT)/artifact"

clean:
	rm -rf -- "$(OUTPUT_ROOT)"
