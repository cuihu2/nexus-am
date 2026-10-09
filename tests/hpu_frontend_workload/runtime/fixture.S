/* 只嵌入本用例已验证的新API交付，不引用旧workload的数据。 */
    .section .rodata.frontend_image,"a",@progbits
    .balign 256
    .globl initial_image
    .type initial_image, @object
initial_image:
    .incbin "image.u32.bin"
    .size initial_image, . - initial_image

    .section .rodata.frontend_golden,"a",@progbits
    .balign 256
    .globl golden_words
    .type golden_words, @object
golden_words:
    .incbin "golden.u32.bin"
    .size golden_words, . - golden_words

    .section .rodata.frontend_writable,"a",@progbits
    .balign 256
    .globl writable_lines
    .type writable_lines, @object
writable_lines:
    .incbin "writable.u8.bin"
    .size writable_lines, . - writable_lines
    .section .note.GNU-stack,"",@progbits
