/* 完整producer Auto窗口、AM尾部guard和独立最终密文golden。 */
    .section .rodata.auto_window,"a",@progbits
    .balign 256
    .globl auto_window
    .type auto_window, @object
auto_window:
    .incbin "window.u32.bin"
    .size auto_window, . - auto_window

    .section .rodata.auto_golden,"a",@progbits
    .balign 256
    .globl auto_golden
    .type auto_golden, @object
auto_golden:
    .incbin "golden.u32.bin"
    .size auto_golden, . - auto_golden

    .section .note.GNU-stack,"",@progbits
