/* 每个应用只链接自己的镜像、最终 golden 和逐 line 写权限。 */
    .section .rodata.ckks_window,"a",@progbits
    .balign 256
    .globl ckks_window
    .type ckks_window, @object
ckks_window:
    .incbin "ckks_window.u32.bin"
    .size ckks_window, . - ckks_window

    .section .rodata.ckks_golden,"a",@progbits
    .balign 256
    .globl ckks_golden
    .type ckks_golden, @object
ckks_golden:
    .incbin "ckks_golden.u32.bin"
    .size ckks_golden, . - ckks_golden

    .section .rodata.ckks_writable,"a",@progbits
    .balign 256
    .globl ckks_writable
    .type ckks_writable, @object
ckks_writable:
    .incbin "ckks_writable.u8.bin"
    .size ckks_writable, . - ckks_writable
    .section .note.GNU-stack,"",@progbits
