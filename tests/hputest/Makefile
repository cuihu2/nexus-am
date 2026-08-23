AM_HOME ?= $(abspath ../..)
ARCH ?= riscv64-xs
CROSS_COMPILE ?= riscv64-linux-gnu-
OUTPUT_ROOT ?= $(CURDIR)/build
JOBS ?= 4

.PHONY: all one validate clean

all:
	@AM_HOME="$(AM_HOME)" ARCH="$(ARCH)" \
	  CROSS_COMPILE="$(CROSS_COMPILE)" JOBS="$(JOBS)" \
	  scripts/build.sh "$(OUTPUT_ROOT)"

one:
	@test -n "$(CASE)" || { echo 'ERROR: use make one CASE=path/from/src/without/.c'; exit 2; }
	@AM_HOME="$(AM_HOME)" ARCH="$(ARCH)" \
	  CROSS_COMPILE="$(CROSS_COMPILE)" JOBS="$(JOBS)" \
	  scripts/build.sh "$(OUTPUT_ROOT)" "$(CASE)"

validate:
	@scripts/validate-build.sh "$(OUTPUT_ROOT)/artifact"

clean:
	rm -rf -- "$(OUTPUT_ROOT)"
