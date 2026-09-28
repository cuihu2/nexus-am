/* PLIC-s (secure PLIC) bare-metal test — register map and shared helpers.
 *
 * Platform: LinkNan security_soc, 1 hart -> PLIC ctx0 = M, ctx1 = S.
 * Offsets follow PLICSecConsts (rocketchip Plic.scala) and the PLIC_S_IRQ
 * spec: sec_src +0x4000 (id-indexed: absolute bit k <-> source id k, bit 0
 * reserved, matching pending/enable), sec_ctrl +0x4100,
 * world_state +0x4200+8h, ws_ack +0x4400+8h ({bit7 done, bit0 echo}),
 * irq_track +0x4600+8ctx ({15:2 id, 1 req_sec, 0 in_service}),
 * sec_status +0x4E00 (bit0 complete-reject, W1C).
 *
 * Interrupt injection: SimMMIO AXI4IntrGenerator @0x4007_0000, 8x32b level
 * registers; line i drives PLIC source i+1 through io.extIntr[i].
 */
#ifndef PLICS_H
#define PLICS_H

#include <stdint.h>

#define PLIC_BASE           0x3C000000UL
#define PLIC_PRIO(id)       (PLIC_BASE + 0x4UL * (id))
#define PLIC_PENDING        (PLIC_BASE + 0x1000UL)
#define PLIC_ENABLE(c)      (PLIC_BASE + 0x2000UL + 0x80UL * (c))
#define PLIC_THRESHOLD(c)   (PLIC_BASE + 0x200000UL + 0x1000UL * (c))
#define PLIC_CLAIM(c)       (PLIC_BASE + 0x200004UL + 0x1000UL * (c))
#define PLIC_SEC_SRC        (PLIC_BASE + 0x4000UL)   /* id-indexed: bit k <-> source id k, bit 0 reserved */
#define PLIC_SEC_CTRL       (PLIC_BASE + 0x4100UL)   /* bit0 routing en, bit1 lock W1T */
#define PLIC_WORLD_STATE(h) (PLIC_BASE + 0x4200UL + 8UL * (h))
#define PLIC_WS_ACK(h)      (PLIC_BASE + 0x4400UL + 8UL * (h))
#define PLIC_IRQ_TRACK(c)   (PLIC_BASE + 0x4600UL + 8UL * (c))
#define PLIC_SEC_STATUS     (PLIC_BASE + 0x4E00UL)

#define CTX_M 0
#define CTX_S 1
#define HART0 0

/* Test sources: intr line N-1 <-> PLIC id N (low bits are unused in minisys) */
#define SEC_ID   10u
#define SEC_BIT  (SEC_ID - 1)
#define NS_ID    11u
#define NS_BIT   (NS_ID - 1)

#define INTR_GEN_BASE 0x40070000UL

/* irq_track field helpers */
#define TRACK(id, sec) (((id) << 2) | ((sec) << 1) | 1u)

/* CSR access */
#define csr_read(reg) ({ uint64_t _v; asm volatile("csrr %0, " #reg : "=r"(_v)); _v; })
#define csr_write(reg, v) asm volatile("csrw " #reg ", %0" :: "r"((uint64_t)(v)))
#define csr_set(reg, v)   asm volatile("csrs " #reg ", %0" :: "r"((uint64_t)(v)))
#define csr_clear(reg, v) asm volatile("csrc " #reg ", %0" :: "r"((uint64_t)(v)))

#define MIE_MEIE (1u << 11)
#define MIE_SEIE (1u << 9)
#define SIP_SEIP (1u << 9)

/* MMIO */
static inline void write32(uint64_t a, uint32_t v) { *(volatile uint32_t *)(uintptr_t)a = v; }
static inline uint32_t read32(uint64_t a) { return *(volatile uint32_t *)(uintptr_t)a; }
static inline void write8(uint64_t a, uint8_t v) { *(volatile uint8_t *)(uintptr_t)a = v; }
static inline uint8_t read8(uint64_t a) { return *(volatile uint8_t *)(uintptr_t)a; }

/* AXI4IntrGenerator level lines, with readback to drain the MMIO write */
static inline void intr_set(uint32_t bit) {
  uint64_t a = INTR_GEN_BASE + (bit / 32) * 4;
  write32(a, read32(a) | (1u << (bit % 32)));
  (void)read32(a);
}
static inline void intr_clear(uint32_t bit) {
  uint64_t a = INTR_GEN_BASE + (bit / 32) * 4;
  write32(a, read32(a) & ~(1u << (bit % 32)));
  (void)read32(a);
}
/* Level gateway: the line must be low (and seen low past the PLIC input
 * synchronizers) before complete, otherwise the source re-pends. */
static inline void intr_clear_wait(uint32_t bit) {
  intr_clear(bit);
  for (volatile int i = 0; i < 500; i++) {}
}

static inline void delay(volatile int n) { for (volatile int i = 0; i < n; i++) {} }

/* ecall service codes (S world -> M orchestrator) */
#define ECALL_TEE_DONE 1u
#define ECALL_REE_DONE 2u
#define ECALL_FAIL     0xFu

static inline void s_ecall(uint64_t code) {
  register uint64_t a7 asm("a7") = code;
  asm volatile("ecall" :: "r"(a7) : "memory");
}

/* World ids for g_cur_world */
#define WORLD_REE 0
#define WORLD_TEE 1

/* Test phases seen by the M trap handler */
#define PH_T2 2
#define PH_T3 3

/* Direct uartlite putchar for single-letter world traces (printf-free) */
extern void __am_uartlite_putchar(char ch);
static inline void trace(char c) { __am_uartlite_putchar(c); }

/* ---- shared state (defined in main.c) ---- */
extern volatile uint32_t g_fail_cnt;
extern volatile uint32_t g_any_fail;
extern volatile uint32_t g_phase;
extern volatile uint32_t g_cur_world;
extern volatile uint32_t g_ree_stage;    /* 0: running, 1: TEE done, back in REE */
extern volatile uint32_t g_tee_stage;    /* 1: TEE entered for T2 service */
extern volatile uint32_t g_ns_serviced;  /* REE S-handler completed NS */
extern volatile uint32_t g_tee_claimed;  /* TEE S-handler claimed SEC (T3) */
extern volatile uint64_t g_m_meip_count; /* MEIP traps taken in M */
extern volatile uint32_t g_t2_ok;        /* T2 sub-check bitmask */
extern volatile uint32_t g_t3_ok;

/* asm entry points */
void enter_world(uint64_t entry, uint64_t sp) __attribute__((noreturn));
extern char m_trap_vec[];
extern char s_trap_vec[];

/* world bodies (world.c) */
void ree_t2(void);
void tee_t2(void);
void tee_t3(void);
uint64_t ree_isr_sp(void);
uint64_t tee_isr_sp(void);
/* M orchestrator continuations (main.c), entered via fresh-M mret */
void m_resume_t3(void) __attribute__((noreturn));
void m_resume_final(void) __attribute__((noreturn));

/* save areas (mtrap.S / strap.S convention: x1..x31 in slots 0..30,
 * mepc/sepc in slot 31; x2 in slot 1) */
extern uint64_t msave_ree[32];
extern uint64_t msave_tee[32];
extern uint64_t ssave_ree[32];
extern uint64_t ssave_tee[32];

/* action protocol between m_handler_c and m_trap_vec */
extern uint64_t g_mret_action; /* 0=resume from area, 1=fresh S entry, 2=fresh M entry */
extern uint64_t g_mret_arg1;   /* action0: save area; action1/2: entry pc */
extern uint64_t g_mret_arg2;   /* action1/2: stack pointer */
extern uint64_t g_sret_area;   /* save area to restore in s_trap_vec */
extern uint64_t g_s_isr_sp;    /* per-world S-ISR runtime stack */

#endif
