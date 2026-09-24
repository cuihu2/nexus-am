/* HPU_SEAL BFV HMUL完整窗口、可写行掩码、AM尾部guard和SEAL golden。 */
    .section .rodata.hmul_window,"a",@progbits
    .balign 256
    .globl hmul_window
    .type hmul_window, @object
hmul_window:
    .incbin "window.u32.bin"
    .size hmul_window, . - hmul_window

    .section .rodata.hmul_golden,"a",@progbits
    .balign 256
    .globl hmul_golden
    .type hmul_golden, @object
hmul_golden:
    .incbin "golden.u32.bin"
    .size hmul_golden, . - hmul_golden

    .section .rodata.hmul_writable_lines,"a",@progbits
    .balign 64
    .globl hmul_writable_lines
    .type hmul_writable_lines, @object
hmul_writable_lines:
    .incbin "writable_lines.u8.bin"
    .size hmul_writable_lines, . - hmul_writable_lines

    .section .note.GNU-stack,"",@progbits
