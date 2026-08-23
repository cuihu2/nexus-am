#include "hpu_test.h"

#if defined(HPU_IT_NEXUS_AM)
#include <klib.h>
#else
#include <stdio.h>
#endif

#if defined(__riscv) && defined(HPU_IT_STANDALONE_HTIF) && \
    HPU_IT_STANDALONE_HTIF
#define HPU_IT_STANDALONE_HTIF_ENABLED 1
/*
 * Optional standalone-Spike termination channel.  The production IT image
 * leaves this disabled and returns through Nexus-AM's normal Nanhu trap.  A
 * locally built functional image enables it so Spike can report the exact
 * testcase return code instead of spinning forever after the Nanhu halt
 * instruction, which standalone Spike intentionally does not interpret.
 */
volatile uint64_t tohost __attribute__((section(".tohost"), aligned(64)));
volatile uint64_t fromhost __attribute__((section(".fromhost"), aligned(64)));

static void hpu_it_htif_exit(int rc) __attribute__((noreturn));

static void hpu_it_htif_exit(int rc) {
    /* Keep fromhost in the linked image; HTIF requires both symbols. */
    fromhost = 0U;
    tohost = ((uint64_t)(uint32_t)rc << 1U) | 1U;
    for (;;) {
        __asm__ volatile("wfi");
    }
}
#else
#define HPU_IT_STANDALONE_HTIF_ENABLED 0
#endif

int main(void) {
    int rc;
#if !HPU_IT_STANDALONE_HTIF_ENABLED
    const char *result;

    printf("HPU_IT_BEGIN case=%s testpoint=%s priority=P%u\n",
           hpu_testcase.case_id,
           hpu_testcase.testpoint_id,
           hpu_testcase.priority);
#endif
    rc = hpu_run_testcase(&hpu_testcase);
#if !HPU_IT_STANDALONE_HTIF_ENABLED
    result = rc != 0 ? "FAIL" : "PASS";
    printf("HPU_IT_END case=%s result=%s rc=%d requirements=0x%x\n",
           hpu_testcase.case_id,
           result,
           rc,
           hpu_testcase.requirements);
#endif
#if HPU_IT_STANDALONE_HTIF_ENABLED
    hpu_it_htif_exit(rc);
#endif
    return rc;
}
