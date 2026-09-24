/* HPU_SEAL BFV RotateRows完整窗口、逐line写权限、尾部guard和SEAL golden。 */
    .section .rodata.rotate_window,"a",@progbits
    .balign 256
    .globl rotate_window
    .type rotate_window, @object
rotate_window:
    .incbin "window.u32.bin"
    .size rotate_window, . - rotate_window

    .section .rodata.rotate_golden,"a",@progbits
    .balign 256
    .globl rotate_golden
    .type rotate_golden, @object
rotate_golden:
    .incbin "golden.u32.bin"
    .size rotate_golden, . - rotate_golden

    .section .rodata.rotate_writable,"a",@progbits
    .balign 64
    .globl rotate_writable
    .type rotate_writable, @object
rotate_writable:
    .incbin "rotate_writable.u8.bin"
    .size rotate_writable, . - rotate_writable

    .section .note.GNU-stack,"",@progbits
