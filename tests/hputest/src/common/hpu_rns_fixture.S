/* Two producer-generated 4096-word inputs for smoke and migrated IT ELFs. */
    .section .rodata.hpu_rns_input_a,"a",@progbits
    .balign 256

    .globl RNS_A
    .type RNS_A, @object
RNS_A:
    .incbin "third_party/inline-asm/outputs/mm/test_data/hardware/images/input_a.u32.bin"
    .size RNS_A, . - RNS_A

    .section .rodata.hpu_rns_input_b,"a",@progbits
    .balign 256
    .globl RNS_B
    .type RNS_B, @object
RNS_B:
    .incbin "third_party/inline-asm/outputs/mm/test_data/hardware/images/input_b.u32.bin"
    .size RNS_B, . - RNS_B

    .section .note.GNU-stack,"",@progbits
