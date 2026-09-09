AM_HOME ?= $(abspath ../..)
ARCH ?= riscv64-xs
CROSS_COMPILE ?= riscv64-linux-gnu-
OUTPUT_ROOT ?= $(CURDIR)/build
JOBS ?= 4
INLINE_ASM_ROOT ?= $(CURDIR)/third_party/inline-asm
HPU_GENERATED_ROOT ?= $(OUTPUT_ROOT)/generated

.PHONY: all group one prepare-inline-asm-mm verify-inline-asm validate clean

all: verify-inline-asm
	@AM_HOME="$(AM_HOME)" ARCH="$(ARCH)" \
	  CROSS_COMPILE="$(CROSS_COMPILE)" JOBS="$(JOBS)" \
	  INLINE_ASM_ROOT="$(INLINE_ASM_ROOT)" \
	  HPU_GENERATED_ROOT="$(HPU_GENERATED_ROOT)" \
	  scripts/build.sh "$(OUTPUT_ROOT)"

group: verify-inline-asm
	@test -n "$(GROUP)" || { echo 'ERROR: use make group GROUP=core|transform|fhe'; exit 2; }
	@AM_HOME="$(AM_HOME)" ARCH="$(ARCH)" \
	  CROSS_COMPILE="$(CROSS_COMPILE)" JOBS="$(JOBS)" \
	  INLINE_ASM_ROOT="$(INLINE_ASM_ROOT)" \
	  HPU_GENERATED_ROOT="$(HPU_GENERATED_ROOT)" \
	  HPU_CASE_GROUP="$(GROUP)" \
	  scripts/build.sh "$(OUTPUT_ROOT)"

one: verify-inline-asm
	@test -n "$(CASE)" || { echo 'ERROR: use make one CASE=path/from/src/without/.c'; exit 2; }
	@AM_HOME="$(AM_HOME)" ARCH="$(ARCH)" \
	  CROSS_COMPILE="$(CROSS_COMPILE)" JOBS="$(JOBS)" \
	  INLINE_ASM_ROOT="$(INLINE_ASM_ROOT)" \
	  HPU_GENERATED_ROOT="$(HPU_GENERATED_ROOT)" \
	  scripts/build.sh "$(OUTPUT_ROOT)" "$(CASE)"

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
