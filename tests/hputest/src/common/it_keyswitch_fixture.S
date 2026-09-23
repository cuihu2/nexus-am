/* AM重定位窗口（含poison scratch/output和尾部guard）及独立producer golden。 */
    .section .rodata.keyswitch_window,"a",@progbits
    .balign 256
    .globl keyswitch_window
    .type keyswitch_window, @object
keyswitch_window:
    .incbin "window.u32.bin"
    .size keyswitch_window, . - keyswitch_window

    .section .rodata.keyswitch_golden,"a",@progbits
    .balign 256
    .globl keyswitch_golden
    .type keyswitch_golden, @object
keyswitch_golden:
    .incbin "golden.u32.bin"
    .size keyswitch_golden, . - keyswitch_golden

    .section .note.GNU-stack,"",@progbits
