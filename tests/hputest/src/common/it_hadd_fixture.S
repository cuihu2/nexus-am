/* HPU_SEAL BFV HADD完整窗口、AM尾部guard和独立SEAL golden。 */
    .section .rodata.hadd_window,"a",@progbits
    .balign 256
    .globl hadd_window
    .type hadd_window, @object
hadd_window:
    .incbin "window.u32.bin"
    .size hadd_window, . - hadd_window

    .section .rodata.hadd_golden,"a",@progbits
    .balign 256
    .globl hadd_golden
    .type hadd_golden, @object
hadd_golden:
    .incbin "golden.u32.bin"
    .size hadd_golden, . - hadd_golden

    .section .note.GNU-stack,"",@progbits
