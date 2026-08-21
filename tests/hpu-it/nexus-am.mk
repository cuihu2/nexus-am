ifndef AM_HOME
$(error AM_HOME must point to an OpenXiangShan/nexus-am checkout)
endif
ifndef CASE
$(error CASE must name one testcase source)
endif
ifndef CASE_ID
$(error CASE_ID must be the testcase ID)
endif
ifndef CASE_KIND
$(error CASE_KIND must name the testcase hpu_case_kind enum)
endif

APP_DIR := $(CURDIR)
NAME := $(CASE_ID)
HPU_IT_MEM_BASE ?= 0x87000000
HPU_IT_MEM_LINES ?= 19201
HPU_IT_SOURCE_FINGERPRINT ?= unfingerprinted
HPU_IT_ENABLE_MMIO ?= 1
HPU_IT_WAIT_IRQ ?= $(HPU_IT_ENABLE_MMIO)
HPU_IT_STANDALONE_HTIF ?= 0
SRCS := runtime/hpu_test.c runtime/hpu_vectors.c runtime/hpu_test_main.c $(CASE)
HPU_IT_FHE_CASE_IDS := HPU_IT_DIR_CMB_004 HPU_IT_DIR_CMB_010 \
                       HPU_IT_DIR_CMB_012 \
                       HPU_IT_DIR_PERF_001 \
                       HPU_IT_DIR_PERF_005 HPU_IT_DIR_PERF_006 \
                       HPU_IT_DIR_APP_001
ifneq ($(filter $(CASE_ID),$(HPU_IT_FHE_CASE_IDS)),)
SRCS += runtime/hpu_fhe.c \
        third_party/inline-asm/outputs/keyswitch/keyswitch.c \
        third_party/inline-asm/outputs/relinearization/relinearization.c \
        third_party/inline-asm/outputs/ciphertext_multiply/ciphertext_multiply.c
CFLAGS += -DHPU_IT_USE_GENERATED_FHE=1
endif
HPU_IT_GENERATED_CASE_IDS := HPU_IT_GEN_PMULT_001 HPU_IT_GEN_CMULT_001 \
                             HPU_IT_GEN_MODUP_001 HPU_IT_GEN_MODDOWN_001 \
                             HPU_IT_DIR_CMB_005 HPU_IT_DIR_CMB_011 \
                             HPU_IT_DIR_CMB_013 HPU_IT_DIR_CMB_014
ifneq ($(filter $(CASE_ID),$(HPU_IT_GENERATED_CASE_IDS)),)
SRCS += runtime/hpu_generated_ops.c \
        third_party/inline-asm/outputs/encode/encode.c \
        third_party/inline-asm/outputs/rescale/rescale.c \
        third_party/inline-asm/outputs/pmult/pmult.c \
        third_party/inline-asm/outputs/cmult/cmult.c \
        third_party/inline-asm/outputs/modup/modup.c \
        third_party/inline-asm/outputs/moddown/moddown.c \
        third_party/inline-asm/outputs/auto/auto.c
endif
ifeq ($(CASE_ID),HPU_IT_DIR_INS_C0_003)
SRCS += third_party/inline-asm/outputs/mm/mm.c
CFLAGS += -DHPU_IT_USE_GENERATED_MM=1
endif
INC_DIR += $(APP_DIR)
CFLAGS += -DHPU_IT_NEXUS_AM=1 -DHPU_IT_ENABLE_MMIO=$(HPU_IT_ENABLE_MMIO) \
          -DHPU_IT_WAIT_IRQ=$(HPU_IT_WAIT_IRQ) \
          -DHPU_IT_STANDALONE_HTIF=$(HPU_IT_STANDALONE_HTIF) \
          -DHPU_IT_MEM_BASE=$(HPU_IT_MEM_BASE) \
          -DHPU_IT_MEM_LINES=$(HPU_IT_MEM_LINES) \
          -DHPU_IT_BUILD_CASE_KIND=$(CASE_KIND) \
          -DHPU_IT_SOURCE_FINGERPRINT=\"$(HPU_IT_SOURCE_FINGERPRINT)\" \
          -DHPU_IT_VECTOR_ROOT=\"$(APP_DIR)\"

include $(AM_HOME)/Makefile.app
