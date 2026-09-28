/* S-mode "OS" bodies for the PLIC-s test: REE (non-secure world) and TEE
 * (secure world), plus the shared S-mode trap handler.
 *
 * The M-mode orchestrator (mini-OpenSBI) drives the world switches; these
 * bodies only service what is routed to their S-context and ecall back.
 * No printf in worlds: single-letter uartlite traces + flags only.
 */
#include "plics.h"

/* per-world runtime stacks and ISR stacks (defined here, tops via macros) */
static uint8_t ree_stack[4096] __attribute__((aligned(16)));
static uint8_t tee_stack[4096] __attribute__((aligned(16)));
static uint8_t ree_isr_stack[2048] __attribute__((aligned(16)));
static uint8_t tee_isr_stack[2048] __attribute__((aligned(16)));

#define REE_SP     ((uint64_t)(ree_stack + sizeof(ree_stack)))
#define TEE_SP     ((uint64_t)(tee_stack + sizeof(tee_stack)))
#define REE_ISR_SP ((uint64_t)(ree_isr_stack + sizeof(ree_isr_stack)))
#define TEE_ISR_SP ((uint64_t)(tee_isr_stack + sizeof(tee_isr_stack)))

uint64_t ree_sp(void) { return REE_SP; }
uint64_t tee_sp(void) { return TEE_SP; }
uint64_t ree_isr_sp(void) { return REE_ISR_SP; }
uint64_t tee_isr_sp(void) { return TEE_ISR_SP; }

static void s_fail(char code);

/* ---- S-mode trap handler (shared; behavior keyed by g_cur_world) ---- */
void s_handler_c(uint64_t *sv) {
  uint64_t scause = csr_read(scause);
  g_sret_area = (uint64_t)sv;
  if ((int64_t)scause >= 0 || (scause & 0x3ff) != 9) {
    s_fail('x');   /* unexpected trap */
    return;
  }
  /* SEIP: claim on this world's S-context (ctx1) */
  uint32_t id = read32(PLIC_CLAIM(CTX_S));
  if (g_cur_world == WORLD_REE) {
    /* T2: NS interrupt delivered to REE after world switch back */
    if (id != NS_ID) { s_fail('c'); return; }                        /* claim id */
    if (read32(PLIC_IRQ_TRACK(CTX_S)) != TRACK(NS_ID, 0)) { s_fail('k'); return; } /* track */
    intr_clear_wait(NS_BIT);
    write32(PLIC_CLAIM(CTX_S), NS_ID);            /* complete, bit31 = 0 (NS) */
    /* complete clears in_service only; id/req_sec stay as claim history */
    if (read32(PLIC_IRQ_TRACK(CTX_S)) & 1) { s_fail('C'); return; }  /* complete */
    if (read32(PLIC_SEC_STATUS) != 0) { s_fail('s'); return; }       /* sec_status */
    g_ns_serviced = 1;
  } else {
    /* T3: secure interrupt routed straight to TEE's S-context */
    if (id != SEC_ID) { s_fail('i'); return; }                       /* claim id */
    if (read32(PLIC_IRQ_TRACK(CTX_S)) != TRACK(SEC_ID, 1)) { s_fail('K'); return; } /* track */
    g_tee_claimed = 1;   /* mainline completes later: keep sec_busy for T4b */
  }
}

static void s_world_init(uint64_t ssave, uint64_t isr_sp, uint32_t world) {
  csr_write(stvec, (uint64_t)s_trap_vec);
  csr_write(sscratch, ssave);
  g_s_isr_sp = isr_sp;
  g_cur_world = world;
  csr_set(sie, MIE_SEIE);
}

static void s_fail(char code) {
  g_fail_cnt++;
  g_any_fail++;
  trace('!');
  trace(code);
}

/* ---- T2: REE runs, gets preempted by the secure interrupt ---- */
void ree_t2(void) {
  s_world_init((uint64_t)ssave_ree, REE_ISR_SP, WORLD_REE);
  trace('R');
  intr_set(SEC_BIT);                       /* secure line up -> routes to M */
  while (g_ree_stage == 0 && g_fail_cnt == 0) {}   /* preempted here */
  trace('r');                              /* resumed after TEE service */
  /* NS line was left high by TEE: must now be delivered to REE (seip) */
  while (!g_ns_serviced && g_fail_cnt == 0) {}
  trace('D');
  s_ecall(ECALL_REE_DONE);
  for (;;) {}
}

/* ---- T2: TEE services the secure interrupt claimed by M as proxy ---- */
void tee_t2(void) {
  s_world_init((uint64_t)ssave_tee, TEE_ISR_SP, WORLD_TEE);
  trace('T');
  if (g_tee_stage != 1) { s_fail('e'); }
  /* T4a: while a secure interrupt is in service, NS must be blocked on
   * every path (sec_busy_hart): no MEIP trap, no SEIP line, but the NS
   * pending bit must be kept (delivered to REE later). */
  uint64_t meip0 = g_m_meip_count;
  intr_set(NS_BIT);
  delay(2000);
  if (g_m_meip_count != meip0) s_fail('m');
  if (csr_read(sip) & SIP_SEIP) s_fail('n');
  if (!(read32(PLIC_PENDING) & (1u << NS_ID))) s_fail('p');
  /* leave NS high on purpose; finish the secure service */
  intr_clear_wait(SEC_BIT);
  trace('d');
  s_ecall(ECALL_TEE_DONE);
  for (;;) {}
}

/* ---- T3: TEE directly claims a secure interrupt on its S-context ---- */
void tee_t3(void) {
  s_world_init((uint64_t)ssave_tee, TEE_ISR_SP, WORLD_TEE);
  trace('t');
  intr_set(SEC_BIT);                       /* world=TEE -> routes to ctx1 */
  while (!g_tee_claimed && g_fail_cnt == 0) {}
  /* T4b: same sec_busy guarantee on the S-ctx direct-claim path */
  uint64_t meip0 = g_m_meip_count;
  intr_set(NS_BIT);
  delay(2000);
  if (g_m_meip_count != meip0) s_fail('M');
  if (csr_read(sip) & SIP_SEIP) s_fail('N');
  if (!(read32(PLIC_PENDING) & (1u << NS_ID))) s_fail('P');
  /* The NS pending bit is latched in the PLIC: clearing the line does NOT
   * drop it. Once the SEC complete below releases sec_busy, the latched NS
   * would legally route to M (spec: NS pending during TEE is forwarded to
   * the M context). Mask NS on ctx0 to end this phase quietly instead. */
  write32(PLIC_ENABLE(CTX_M), 1u << SEC_ID);
  intr_clear_wait(NS_BIT);
  intr_clear_wait(SEC_BIT);
  write32(PLIC_CLAIM(CTX_S), (1u << 31) | SEC_ID);  /* complete, bit31 = 1 (S) */
  if (read32(PLIC_IRQ_TRACK(CTX_S)) & 1) s_fail('o');
  if (read32(PLIC_SEC_STATUS) != 0) s_fail('t');
  trace('D');
  s_ecall(ECALL_TEE_DONE);
  for (;;) {}
}
