/* 仅计算用例链接生产者的 golden 和模表，来源为已校验交付目录。 */
    .section .rodata.hpu_rns_expected,"a",@progbits
    .balign 256

    .globl RNS_EXPECTED
    .type RNS_EXPECTED, @object
RNS_EXPECTED:
    .incbin "images/expected.u32.bin"
    .size RNS_EXPECTED, . - RNS_EXPECTED

    .section .rodata.hpu_rns_mod_ctx,"a",@progbits
    .balign 256
    .globl RNS_MOD_CTX
    .type RNS_MOD_CTX, @object
RNS_MOD_CTX:
    .incbin "constants/mod_ctx.u32.bin"
    .size RNS_MOD_CTX, . - RNS_MOD_CTX

    .section .note.GNU-stack,"",@progbits
