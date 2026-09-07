/* 两组 4096 项输入；汇编器从 Makefile 指定的已校验交付目录取数。 */
    .section .rodata.hpu_rns_input_a,"a",@progbits
    .balign 256

    .globl RNS_A
    .type RNS_A, @object
RNS_A:
    .incbin "images/input_a.u32.bin"
    .size RNS_A, . - RNS_A

    .section .rodata.hpu_rns_input_b,"a",@progbits
    .balign 256
    .globl RNS_B
    .type RNS_B, @object
RNS_B:
    .incbin "images/input_b.u32.bin"
    .size RNS_B, . - RNS_B

    .section .note.GNU-stack,"",@progbits
