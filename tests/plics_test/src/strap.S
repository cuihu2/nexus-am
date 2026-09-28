/* S-mode trap vector for the PLIC-s test worlds (REE and TEE share it).
 * sscratch holds the per-world save area; s_handler_c() runs on the
 * per-world ISR stack (g_s_isr_sp) and restores from g_sret_area.
 * Only SEIP is expected; no sepc adjustment (interrupt-only handler). */
  .section .text, "ax"
  .p2align 4
  .globl s_trap_vec
s_trap_vec:
  csrrw sp, sscratch, sp    # sp = save area, sscratch = trapped sp
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
  csrr t0, sscratch         # trapped sp -> x2 slot
  sd t0, 8(sp)
  csrw sscratch, sp         # restore convention: sscratch = area base
  csrr t0, sepc
  sd t0, 248(sp)
  mv a0, sp                 # arg: save area
  la t0, g_s_isr_sp
  ld sp, 0(t0)              # per-world ISR stack
  call s_handler_c
  la t0, g_sret_area
  ld t6, 0(t0)
  ld t0, 248(t6)
  csrw sepc, t0
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
  ld x2,   8(t6)
  ld x31, 240(t6)
  sret
