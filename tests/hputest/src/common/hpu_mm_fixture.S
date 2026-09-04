/* Producer golden and modulus context are linked only into the MM testcase. */
    .section .rodata.hpu_rns_expected,"a",@progbits
    .balign 256

    .globl RNS_EXPECTED
    .type RNS_EXPECTED, @object
RNS_EXPECTED:
    .incbin "third_party/inline-asm/outputs/mm/test_data/hardware/images/expected.u32.bin"
    .size RNS_EXPECTED, . - RNS_EXPECTED

    .section .rodata.hpu_rns_mod_ctx,"a",@progbits
    .balign 256
    .globl RNS_MOD_CTX
    .type RNS_MOD_CTX, @object
RNS_MOD_CTX:
    .incbin "third_party/inline-asm/outputs/mm/test_data/hardware/constants/mod_ctx.u32.bin"
    .size RNS_MOD_CTX, . - RNS_MOD_CTX

    .section .note.GNU-stack,"",@progbits
