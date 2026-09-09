/* producer 镜像、独立输出 golden 和显式派生的中间数学参考。 */
    .section .rodata.bconv_window,"a",@progbits
    .balign 256
    .globl bconv_window
    .type bconv_window, @object
bconv_window:
    .incbin "window.u32.bin"
    .size bconv_window, . - bconv_window

    .section .rodata.bconv_golden,"a",@progbits
    .balign 256
    .globl bconv_golden
    .type bconv_golden, @object
bconv_golden:
    .incbin "golden_p.u32.bin"
    .size bconv_golden, . - bconv_golden

    .section .rodata.bconv_normalized,"a",@progbits
    .balign 256
    .globl bconv_normalized
    .type bconv_normalized, @object
bconv_normalized:
    .incbin "normalized_cpu.u32.bin"
    .size bconv_normalized, . - bconv_normalized

    .section .note.GNU-stack,"",@progbits
