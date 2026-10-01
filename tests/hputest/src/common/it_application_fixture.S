/* Application cases import one validated image, all step goldens and a write mask. */
    .section .rodata.application_window,"a",@progbits
    .balign 256
    .globl application_window
    .type application_window, @object
application_window:
    .incbin "application_window.u32.bin"
    .size application_window, . - application_window

    .section .rodata.application_golden,"a",@progbits
    .balign 256
    .globl application_golden
    .type application_golden, @object
application_golden:
    .incbin "application_golden.u32.bin"
    .size application_golden, . - application_golden

    .section .rodata.application_writable,"a",@progbits
    .balign 256
    .globl application_writable
    .type application_writable, @object
application_writable:
    .incbin "application_writable.u8.bin"
    .size application_writable, . - application_writable
    .section .note.GNU-stack,"",@progbits
