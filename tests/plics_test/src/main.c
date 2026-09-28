/* PLIC-s (secure PLIC) bare-metal test — M-mode orchestrator (mini-OpenSBI).
 *
 * Runs on LinkNan security_soc (1 hart, PLIC ctx0=M / ctx1=S) in the minisys
 * simulation. Interrupts are self-injected through the SimMMIO
 * AXI4IntrGenerator. Worlds (REE/TEE "OS") are S-mode bodies entered via
 * mret; see PLIC_S_IRQ.md for the routing/claim/complete rules under test.
 *
 *   T1 register face: reset values (world_state=TEE), ws_ack handshake,
 *      sec_src / sec_ctrl / irq_track / sec_status access
 *   T2 secure IRQ during REE: routes to M-ctx (MEIP), M proxy-claims,
 *      3-step world switch to TEE, TEE services, complete(bit31=1),
 *      switch back, REE resumed with full register context; the NS
 *      interrupt pended during TEE service is then delivered to REE (SEIP)
 *   T3 secure IRQ during TEE: routes to S-ctx (SEIP), TEE direct claim,
 *      complete(bit31=1) on ctx1
 *   T4 sec_busy_hart: NS blocked on all paths during secure service
 *      (checked on both the M-proxy path, T2, and the S-direct path, T3)
 *   T5 complete protection: empty / wrong-id / wrong-flag / replay
 *      completes are rejected and set sec_status bit0 (W1C)
 *   T6 sec_ctrl lock freezes sec_src and the routing-enable bit
 */
#include <klib.h>
#include "plics.h"

/* ---- shared state ---- */
volatile uint32_t g_fail_cnt;
volatile uint32_t g_any_fail;   /* sticky: set by every m_fail/s_fail */
volatile uint32_t g_phase;
volatile uint32_t g_cur_world;
volatile uint32_t g_ree_stage;
volatile uint32_t g_tee_stage;
volatile uint32_t g_ns_serviced;
volatile uint32_t g_tee_claimed;
volatile uint64_t g_m_meip_count;
volatile uint32_t g_t2_ok;
volatile uint32_t g_t3_ok;

uint64_t g_mret_action, g_mret_arg1, g_mret_arg2;
uint64_t g_sret_area, g_s_isr_sp;

/* trap save areas (x1..x31 slots 0..30, mepc/sepc slot 31) */
uint64_t msave_ree[32] __attribute__((aligned(16)));
uint64_t msave_tee[32] __attribute__((aligned(16)));
uint64_t ssave_ree[32] __attribute__((aligned(16)));
uint64_t ssave_tee[32] __attribute__((aligned(16)));

uint64_t ree_sp(void);
uint64_t tee_sp(void);

/* linker-provided M runtime stack top (loader64.ld) */
extern char _stack_pointer;
#define M_STACK_TOP ((uint64_t)&_stack_pointer)

static void m_fail(const char *msg) {
  g_fail_cnt++;
  g_any_fail++;
  printf("[PLIC-S][FAIL] %s\n", msg);
}

/* ---- world_state switch, PLIC_S_IRQ section 4 three-step sequence ---- */
static int wait_ws_ack(uint32_t expect) {
  for (int i = 0; i < 100000; i++) {
    uint32_t ack = read32(PLIC_WS_ACK(HART0)) & 0x81;
    if (ack == (0x80u | expect)) return 0;
  }
  return -1;
}

static int world_switch(uint32_t tee) {
  csr_clear(mie, MIE_MEIE | MIE_SEIE);      /* step 1: mask */
  write32(PLIC_WORLD_STATE(HART0), tee);    /* step 2: sync + wait hw ACK */
  if (wait_ws_ack(tee) != 0) return -1;
  csr_set(mie, MIE_MEIE | MIE_SEIE);        /* step 3: restore */
  return 0;
}

/* ================= T1: register face ================= */
static void t1_register_face(void) {
  int bad = 0;
  /* reset values: world comes up in TEE, everything else quiescent */
  if ((read32(PLIC_WORLD_STATE(HART0)) & 1) != 1) { m_fail("T1 world_state reset != TEE"); bad = 1; }
  if ((read32(PLIC_WS_ACK(HART0)) & 0x81) != 0x81) { m_fail("T1 ws_ack reset"); bad = 1; }
  if (read32(PLIC_SEC_CTRL) != 0) { m_fail("T1 sec_ctrl reset != 0"); bad = 1; }
  if (read32(PLIC_IRQ_TRACK(CTX_M)) != 0 || read32(PLIC_IRQ_TRACK(CTX_S)) != 0) {
    m_fail("T1 irq_track reset != 0"); bad = 1;
  }
  if (read32(PLIC_SEC_STATUS) != 0) { m_fail("T1 sec_status reset != 0"); bad = 1; }
  /* ws_ack handshake both ways */
  write32(PLIC_WORLD_STATE(HART0), 0);
  if (wait_ws_ack(0) != 0) { m_fail("T1 ws_ack REE timeout"); bad = 1; }
  if ((read32(PLIC_WORLD_STATE(HART0)) & 1) != 0) { m_fail("T1 world_state REE"); bad = 1; }
  write32(PLIC_WORLD_STATE(HART0), 1);
  if (wait_ws_ack(1) != 0) { m_fail("T1 ws_ack TEE timeout"); bad = 1; }
  write32(PLIC_WORLD_STATE(HART0), 0);      /* leave in REE */
  if (wait_ws_ack(0) != 0) { m_fail("T1 ws_ack back-to-REE timeout"); bad = 1; }
  /* sec_src bitmap is id-indexed: byte SEC_ID/8, bit SEC_ID%8 (bit 0 reserved) */
  uint64_t sbyte = PLIC_SEC_SRC + SEC_ID / 8;
  uint8_t smask = 1u << (SEC_ID % 8);
  if ((read8(sbyte) & smask) != 0) { m_fail("T1 sec_src reset != 0"); bad = 1; }
  write8(sbyte, smask);
  if ((read8(sbyte) & smask) != smask) { m_fail("T1 sec_src write"); bad = 1; }
  write8(sbyte, 0);
  if ((read8(sbyte) & smask) != 0) { m_fail("T1 sec_src clear"); bad = 1; }
  printf("[PLIC-S] T1 register face: %s\n", bad ? "FAIL" : "PASS");
}

/* ---- static routing setup shared by T2/T3/T5 ---- */
static void setup_routing(void) {
  /* PMP: with no entries the default policy denies all S/U accesses
   * (normally OpenSBI opens this). One NAPOT-all RWX entry for the worlds. */
  csr_write(pmpaddr0, (uint64_t)-1);
  csr_write(pmpcfg0, 0x1f);                          /* A=NAPOT | R|W|X */
  write32(PLIC_SEC_CTRL, 1);                         /* enable security routing */
  uint64_t sbyte = PLIC_SEC_SRC + SEC_ID / 8;
  write8(sbyte, 1u << (SEC_ID % 8));           /* SEC_ID is secure */
  write32(PLIC_PRIO(SEC_ID), 7);
  write32(PLIC_PRIO(NS_ID), 7);
  write32(PLIC_ENABLE(CTX_M), (1u << SEC_ID) | (1u << NS_ID));
  write32(PLIC_ENABLE(CTX_S), (1u << SEC_ID) | (1u << NS_ID));
  write32(PLIC_THRESHOLD(CTX_M), 0);
  write32(PLIC_THRESHOLD(CTX_S), 0);
  csr_write(mideleg, MIE_SEIE);                      /* SEIP to S worlds */
  csr_set(mie, MIE_MEIE | MIE_SEIE);
  csr_write(mtvec, (uint64_t)m_trap_vec);
}

/* ================= M-mode trap handler ================= */
void m_handler_c(uint64_t *sv) {
  uint64_t mcause = csr_read(mcause);
  if ((int64_t)mcause < 0) {
    if ((mcause & 0x3ff) != 11) { m_fail("M: unexpected interrupt"); goto out_resume; }
    g_m_meip_count++;
    if (g_phase != PH_T2) { m_fail("M: unexpected MEIP phase"); goto out_resume; }
    /* T2: secure interrupt reached M while world=REE (proxy path) */
    if (csr_read(mip) & (1u << 9)) m_fail("T2 secure leaked to S-ctx");
    uint32_t id = read32(PLIC_CLAIM(CTX_M));
    if (id != SEC_ID) { m_fail("T2 M claim id"); goto out_resume; }
    uint32_t trk = read32(PLIC_IRQ_TRACK(CTX_M));
    if (trk != TRACK(SEC_ID, 1)) {
      printf("[PLIC-S][FAIL] T2 M irq_track raw=%lx (expect %lx)\n", trk, TRACK(SEC_ID, 1));
      g_fail_cnt++;
      goto out_resume;
    }
    /* world switch REE -> TEE, then enter TEE fresh (REE ctx stays saved) */
    if (world_switch(1) != 0) { m_fail("T2 ws to TEE"); goto out_resume; }
    g_tee_stage = 1;
    csr_write(mscratch, (uint64_t)msave_tee);
    g_mret_action = 1;
    g_mret_arg1 = (uint64_t)tee_t2;
    g_mret_arg2 = tee_sp();
    return;
  }
  if ((mcause & 0x3ff) != 9) {
    printf("[PLIC-S][FAIL] M exception mcause=%lx mepc=%lx mtval=%lx\n",
           mcause, csr_read(mepc), csr_read(mtval));
    g_fail_cnt++;
    goto out_resume;
  }
  uint64_t a7 = sv[16];                 /* x17 */
  if (g_phase == PH_T2 && a7 == ECALL_TEE_DONE) {
    /* TEE finished the secure service: complete on the proxy ctx (bit31=1) */
    write32(PLIC_CLAIM(CTX_M), (1u << 31) | SEC_ID);
    /* complete clears in_service only; id/req_sec stay as claim history */
    if (read32(PLIC_IRQ_TRACK(CTX_M)) & 1) m_fail("T2 M complete rejected");
    if (read32(PLIC_SEC_STATUS) != 0) m_fail("T2 sec_status after complete");
    /* Switch back before resuming: the NS line TEE left high must not trap
     * into M (with world=REE it routes to S-ctx only). MIE is 0 here, so
     * ordering is safe. */
    if (world_switch(0) != 0) m_fail("T2 ws to REE");
    /* REE is resumed (not fresh-entered), so restore its S-side context that
     * TEE's entry overwrote: world id, ISR stack and sscratch save area */
    g_cur_world = WORLD_REE;
    g_s_isr_sp = ree_isr_sp();
    csr_write(sscratch, (uint64_t)ssave_ree);
    g_ree_stage = 1;
    csr_write(mscratch, (uint64_t)msave_ree);
    g_mret_action = 0;                  /* resume REE with full reg context */
    g_mret_arg1 = (uint64_t)msave_ree;
    return;
  }
  if (g_phase == PH_T2 && a7 == ECALL_REE_DONE) {
    g_t2_ok = (g_fail_cnt == 0);
    g_mret_action = 2;
    g_mret_arg1 = (uint64_t)m_resume_t3;
    g_mret_arg2 = M_STACK_TOP;
    return;
  }
  if (g_phase == PH_T3 && a7 == ECALL_TEE_DONE) {
    g_t3_ok = (g_fail_cnt == 0);
    g_mret_action = 2;
    g_mret_arg1 = (uint64_t)m_resume_final;
    g_mret_arg2 = M_STACK_TOP;
    return;
  }
  m_fail("M: bad ecall");
out_resume:
  /* best-effort: abort the world test, go straight to the final phase */
  g_mret_action = 2;
  g_mret_arg1 = (uint64_t)m_resume_final;
  g_mret_arg2 = M_STACK_TOP;
}

/* ================= T5: complete protection (M-mode, polled) ================= */
static void t5_complete_protection(void) {
  int bad = 0;
  uint32_t st;
  intr_clear(SEC_BIT);
  intr_clear(NS_BIT);
  delay(1000);
  /* (a) empty complete: nothing in service on ctx0 */
  write32(PLIC_CLAIM(CTX_M), (1u << 31) | SEC_ID);
  st = read32(PLIC_SEC_STATUS);
  if ((st & 1) != 1) { m_fail("T5a empty complete not rejected"); bad = 1; }
  write32(PLIC_SEC_STATUS, 1);                      /* W1C */
  if (read32(PLIC_SEC_STATUS) != 0) { m_fail("T5a sec_status W1C"); bad = 1; }
  /* claim a secure interrupt on ctx0 (world=REE -> M path) */
  intr_set(SEC_BIT);
  uint32_t id = 0;
  for (int i = 0; i < 100000 && id == 0; i++) id = read32(PLIC_CLAIM(CTX_M));
  if (id != SEC_ID) { m_fail("T5 claim"); bad = 1; goto t5_out; }
  uint32_t trk = read32(PLIC_IRQ_TRACK(CTX_M));
  if (trk != TRACK(SEC_ID, 1)) {
    printf("[PLIC-S][FAIL] T5 track raw=%lx sec_src_b1=%x (expect %lx)\n",
           trk, read8(PLIC_SEC_SRC + SEC_ID / 8), TRACK(SEC_ID, 1));
    g_fail_cnt++; bad = 1;
  }
  /* (b) wrong id */
  write32(PLIC_CLAIM(CTX_M), (1u << 31) | NS_ID);
  if ((read32(PLIC_SEC_STATUS) & 1) != 1) { m_fail("T5b wrong id not rejected"); bad = 1; }
  if (read32(PLIC_IRQ_TRACK(CTX_M)) != TRACK(SEC_ID, 1)) { m_fail("T5b track changed"); bad = 1; }
  write32(PLIC_SEC_STATUS, 1);
  /* (c) wrong security flag */
  write32(PLIC_CLAIM(CTX_M), SEC_ID);               /* bit31 = 0 */
  if ((read32(PLIC_SEC_STATUS) & 1) != 1) { m_fail("T5c wrong flag not rejected"); bad = 1; }
  if (read32(PLIC_IRQ_TRACK(CTX_M)) != TRACK(SEC_ID, 1)) { m_fail("T5c track changed"); bad = 1; }
  write32(PLIC_SEC_STATUS, 1);
  /* (d) valid complete, then replay */
  write32(PLIC_CLAIM(CTX_M), (1u << 31) | SEC_ID);
  if (read32(PLIC_IRQ_TRACK(CTX_M)) & 1) { m_fail("T5d valid complete"); bad = 1; }
  if (read32(PLIC_SEC_STATUS) != 0) { m_fail("T5d false reject"); bad = 1; }
  write32(PLIC_CLAIM(CTX_M), (1u << 31) | SEC_ID);
  if ((read32(PLIC_SEC_STATUS) & 1) != 1) { m_fail("T5d replay not rejected"); bad = 1; }
  write32(PLIC_SEC_STATUS, 1);
t5_out:
  intr_clear_wait(SEC_BIT);
  printf("[PLIC-S] T5 complete protection: %s\n", bad ? "FAIL" : "PASS");
}

/* ================= T6: sec_ctrl lock (destructive, runs last) ================= */
static void t6_lock(void) {
  int bad = 0;
  uint64_t sbyte = PLIC_SEC_SRC + SEC_ID / 8;
  uint8_t smask = 1u << (SEC_ID % 8);
  write32(PLIC_SEC_CTRL, 3);                        /* enable + lock (W1T) */
  if (read32(PLIC_SEC_CTRL) != 3) { m_fail("T6 lock set"); bad = 1; }
  write8(sbyte, 0);                                 /* must be ignored */
  if ((read8(sbyte) & smask) != smask) { m_fail("T6 sec_src not frozen"); bad = 1; }
  write32(PLIC_SEC_CTRL, 1);                        /* must not clear bit0 */
  if (read32(PLIC_SEC_CTRL) != 3) { m_fail("T6 routing enable not frozen"); bad = 1; }
  printf("[PLIC-S] T6 sec_ctrl lock: %s\n", bad ? "FAIL" : "PASS");
}

/* ================= orchestrator continuations ================= */
void m_resume_t3(void) {
  printf("[PLIC-S] T2 secure IRQ in REE -> TEE switch: %s\n",
         (g_t2_ok && g_ns_serviced) ? "PASS" : "FAIL");
  printf("[PLIC-S] T4a sec_busy blocks NS (M-proxy path): %s\n",
         g_t2_ok ? "PASS" : "FAIL");
  g_phase = PH_T3;
  g_fail_cnt = 0;
  if (world_switch(1) != 0) m_fail("T3 ws to TEE");
  csr_write(mscratch, (uint64_t)msave_tee);
  enter_world((uint64_t)tee_t3, tee_sp());
}

void m_resume_final(void) {
  /* kill all interrupt paths first: the abort path can arrive here with
   * mie/mstatus.MIE still live and stale lines pending */
  csr_clear(mie, MIE_MEIE | MIE_SEIE);
  csr_clear(mstatus, 0x8);
  /* the abort path may have left an interrupt claimed on ctx0 (T2 fails after
   * the proxy claim): complete it so the level gateway can re-pend for T5 */
  uint32_t trk = read32(PLIC_IRQ_TRACK(CTX_M));
  if (trk & 1) write32(PLIC_CLAIM(CTX_M), ((trk >> 1) & 1) << 31 | ((trk >> 2) & 0x3fff));
  intr_clear(SEC_BIT);
  intr_clear(NS_BIT);
  delay(1000);
  if (g_phase == PH_T3) {
    printf("[PLIC-S] T3 secure IRQ in TEE (S-ctx direct): %s\n",
           g_t3_ok ? "PASS" : "FAIL");
    printf("[PLIC-S] T4b sec_busy blocks NS (S-direct path): %s\n",
           g_t3_ok ? "PASS" : "FAIL");
  } else {
    printf("[PLIC-S] aborted early, see FAIL lines above\n");
  }
  g_fail_cnt = 0;
  /* world is REE after T2; T3 leaves world=TEE: force REE for T5 */
  write32(PLIC_WORLD_STATE(HART0), 0);
  if (wait_ws_ack(0) != 0) m_fail("final ws to REE");
  t5_complete_protection();
  t6_lock();
  if (g_fail_cnt == 0 && g_any_fail == 0) {
    printf("[PLIC-S] RESULT: PLICS_TEST_PASS\n");
  } else {
    printf("[PLIC-S] RESULT: PLICS_TEST_FAIL (%d)\n", (int)g_any_fail);
  }
  for (;;) {}
}

/* ================= main (M-mode) ================= */
int main(const char *args) {
  (void)args;
  printf("[PLIC-S] bare-metal secure-PLIC test boot\n");
  t1_register_face();
  setup_routing();
  /* T2: enter REE; the secure interrupt arrives while REE runs */
  g_fail_cnt = 0;
  g_phase = PH_T2;
  csr_write(mscratch, (uint64_t)msave_ree);
  if (world_switch(0) != 0) m_fail("T2 ws to REE");
  printf("[PLIC-S] setup done, entering REE (mret)\n");
  enter_world((uint64_t)ree_t2, ree_sp());
}
