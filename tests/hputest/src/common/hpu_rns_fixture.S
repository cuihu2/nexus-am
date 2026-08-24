/* Two deterministic one-modulus RNS polynomials, embedded in every 001 ELF. */
    .section .rodata.hpu_rns_fixture,"a",@progbits
    .balign 256

    .globl hpu_rns_input_a
    .type hpu_rns_input_a, @object
hpu_rns_input_a:
    .set hpu_fixture_index, 0
    .rept 4096
    .4byte hpu_fixture_index * 7 + 3
    .set hpu_fixture_index, hpu_fixture_index + 1
    .endr
    .size hpu_rns_input_a, . - hpu_rns_input_a

    .balign 256
    .globl hpu_rns_input_b
    .type hpu_rns_input_b, @object
hpu_rns_input_b:
    .set hpu_fixture_index, 0
    .rept 4096
    .4byte hpu_fixture_index * 11 + 5
    .set hpu_fixture_index, hpu_fixture_index + 1
    .endr
    .size hpu_rns_input_b, . - hpu_rns_input_b

    .section .note.GNU-stack,"",@progbits
