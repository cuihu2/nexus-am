/* 只读镜像与独立 golden；链接器仅保留当前算子引用的节。 */
    .section .rodata.transform_ntt_image,"a",@progbits
    .balign 256
    .globl transform_ntt_image
    .type transform_ntt_image, @object
transform_ntt_image:
    .incbin "ntt/window.u32.bin"
    .size transform_ntt_image, . - transform_ntt_image

    .section .rodata.transform_ntt_golden,"a",@progbits
    .balign 256
    .globl transform_ntt_golden
    .type transform_ntt_golden, @object
transform_ntt_golden:
    .incbin "ntt/golden.u32.bin"
    .size transform_ntt_golden, . - transform_ntt_golden

    .section .rodata.transform_intt_image,"a",@progbits
    .balign 256
    .globl transform_intt_image
    .type transform_intt_image, @object
transform_intt_image:
    .incbin "intt/window.u32.bin"
    .size transform_intt_image, . - transform_intt_image

    .section .rodata.transform_intt_golden,"a",@progbits
    .balign 256
    .globl transform_intt_golden
    .type transform_intt_golden, @object
transform_intt_golden:
    .incbin "intt/golden.u32.bin"
    .size transform_intt_golden, . - transform_intt_golden

    .section .note.GNU-stack,"",@progbits
