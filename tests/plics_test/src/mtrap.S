/* M-mode trap vector and world-entry trampoline for the PLIC-s test.
 *
 * Convention: mscratch always holds the base of the 32-slot save area for
 * the currently-running world (set by the orchestrator before every mret).
 * m_handler_c() returns the next action through globals:
 *   g_mret_action = 0: resume interrupted ctx, restore from g_mret_arg1 area
 *   g_mret_action = 1: fresh S-world entry at g_mret_arg1 with sp=g_mret_arg2
 *   g_mret_action = 2: fresh M-mode entry at g_mret_arg1 with sp=g_mret_arg2
 */
  .section .text, "ax"
  .p2align 4
  .globl m_trap_vec
m_trap_vec:
  csrrw sp, mscratch, sp    # sp = save area, mscratch = trapped sp
  sd x1,   0(sp)
  sd x3,  16(sp)
  sd x4,  24(sp)
  sd x5,  32(sp)
  sd x6,  40(sp)
  sd x7,  48(sp)
  sd x8,  56(sp)
  sd x9,  64(sp)
  sd x10, 72(sp)
  sd x11, 80(sp)
  sd x12, 88(sp)
  sd x13, 96(sp)
  sd x14, 104(sp)
  sd x15, 112(sp)
  sd x16, 120(sp)
  sd x17, 128(sp)
  sd x18, 136(sp)
  sd x19, 144(sp)
  sd x20, 152(sp)
  sd x21, 160(sp)
  sd x22, 168(sp)
  sd x23, 176(sp)
  sd x24, 184(sp)
  sd x25, 192(sp)
  sd x26, 200(sp)
  sd x27, 208(sp)
  sd x28, 216(sp)
  sd x29, 224(sp)
  sd x30, 232(sp)
  sd x31, 240(sp)
  csrr t0, mscratch         # trapped sp -> x2 slot
  sd t0, 8(sp)
  csrw mscratch, sp         # restore convention: mscratch = area base
  csrr t0, mepc
  sd t0, 248(sp)
  mv a0, sp                 # arg: save area
  la sp, _stack_pointer     # M runtime stack
  call m_handler_c
  la t0, g_mret_action
  ld a0, 0(t0)
  la t0, g_mret_arg1
  ld a1, 0(t0)
  la t0, g_mret_arg2
  ld a2, 0(t0)
  beqz a0, .Lm_resume
  li t0, 1
  beq a0, t0, .Lm_fresh_s
  # action 2: fresh M-mode entry (orchestrator continuation)
  csrw mepc, a1
  mv sp, a2
  li t0, 0x1800
  csrs mstatus, t0          # MPP=M
  # MPIE left 0: continuations run with MIE=0 so a stale pending interrupt
  # cannot re-trap in the window before they mask mie (abort-path livelock)
  mret
.Lm_fresh_s:
  csrw mepc, a1
  mv sp, a2
  li t0, 0x1800
  csrc mstatus, t0
  li t0, 0x0800
  csrs mstatus, t0          # MPP=S
  li t0, 0x0082
  csrs mstatus, t0          # MPIE=1, SIE=1
  mret
.Lm_resume:
  mv t6, a1                 # save area to restore from
  ld t0, 248(t6)
  csrw mepc, t0
  li t0, 0x0080
  csrs mstatus, t0          # MPIE=1 -> MIE on mret
  ld x1,   0(t6)
  ld x3,  16(t6)
  ld x4,  24(t6)
  ld x5,  32(t6)
  ld x6,  40(t6)
  ld x7,  48(t6)
  ld x8,  56(t6)
  ld x9,  64(t6)
  ld x10, 72(t6)
  ld x11, 80(t6)
  ld x12, 88(t6)
  ld x13, 96(t6)
  ld x14, 104(t6)
  ld x15, 112(t6)
  ld x16, 120(t6)
  ld x17, 128(t6)
  ld x18, 136(t6)
  ld x19, 144(t6)
  ld x20, 152(t6)
  ld x21, 160(t6)
  ld x22, 168(t6)
  ld x23, 176(t6)
  ld x24, 184(t6)
  ld x25, 192(t6)
  ld x26, 200(t6)
  ld x27, 208(t6)
  ld x28, 216(t6)
  ld x29, 224(t6)
  ld x30, 232(t6)
  ld x2,   8(t6)            # sp before t6: base still needed below
  ld x31, 240(t6)
  mret

/* enter_world(entry, sp): fresh S-mode entry, never returns.
 * Caller must have set world_state, mscratch, and mie beforehand. */
  .p2align 4
  .globl enter_world
enter_world:
  csrw mepc, a0
  mv sp, a1
  li t0, 0x1800
  csrc mstatus, t0
  li t0, 0x0800
  csrs mstatus, t0          # MPP=S
  li t0, 0x0082
  csrs mstatus, t0          # MPIE=1, SIE=1
  mret
