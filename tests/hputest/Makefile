AM_HOME ?= $(abspath ../..)
ARCH ?= riscv64-xs
CROSS_COMPILE ?= riscv64-linux-gnu-
OUTPUT_ROOT ?= $(CURDIR)/build
JOBS ?= 4
INLINE_ASM_ROOT ?= $(CURDIR)/third_party/inline-asm

.PHONY: all one verify-inline-asm validate clean

all: verify-inline-asm
	@AM_HOME="$(AM_HOME)" ARCH="$(ARCH)" \
	  CROSS_COMPILE="$(CROSS_COMPILE)" JOBS="$(JOBS)" \
	  scripts/build.sh "$(OUTPUT_ROOT)"

one: verify-inline-asm
	@test -n "$(CASE)" || { echo 'ERROR: use make one CASE=path/from/src/without/.c'; exit 2; }
	@AM_HOME="$(AM_HOME)" ARCH="$(ARCH)" \
	  CROSS_COMPILE="$(CROSS_COMPILE)" JOBS="$(JOBS)" \
	  scripts/build.sh "$(OUTPUT_ROOT)" "$(CASE)"

verify-inline-asm:
	@scripts/verify-inline-asm-encodings.sh "$(INLINE_ASM_ROOT)"

validate:
	@scripts/validate-build.sh "$(OUTPUT_ROOT)/artifact"

clean:
	rm -rf -- "$(OUTPUT_ROOT)"
