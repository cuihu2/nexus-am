/* Producer golden and modulus context are linked only into the MM testcase. */
    .section .rodata.hpu_rns_expected,"a",@progbits
    .balign 256

    .globl hpu_rns_expected
    .type hpu_rns_expected, @object
hpu_rns_expected:
    .incbin "third_party/inline-asm/outputs/mm/test_data/hardware/images/expected.u32.bin"
    .size hpu_rns_expected, . - hpu_rns_expected

    .section .rodata.hpu_rns_mod_ctx,"a",@progbits
    .balign 256
    .globl hpu_rns_mod_ctx
    .type hpu_rns_mod_ctx, @object
hpu_rns_mod_ctx:
    .incbin "third_party/inline-asm/outputs/mm/test_data/hardware/constants/mod_ctx.u32.bin"
    .size hpu_rns_mod_ctx, . - hpu_rns_mod_ctx

    .section .note.GNU-stack,"",@progbits
