/* Two producer-generated 4096-word inputs, embedded in every smoke-001 ELF. */
    .section .rodata.hpu_rns_input_a,"a",@progbits
    .balign 256

    .globl hpu_rns_input_a
    .type hpu_rns_input_a, @object
hpu_rns_input_a:
    .incbin "third_party/inline-asm/outputs/mm/test_data/hardware/images/input_a.u32.bin"
    .size hpu_rns_input_a, . - hpu_rns_input_a

    .section .rodata.hpu_rns_input_b,"a",@progbits
    .balign 256
    .globl hpu_rns_input_b
    .type hpu_rns_input_b, @object
hpu_rns_input_b:
    .incbin "third_party/inline-asm/outputs/mm/test_data/hardware/images/input_b.u32.bin"
    .size hpu_rns_input_b, . - hpu_rns_input_b

    .section .note.GNU-stack,"",@progbits
