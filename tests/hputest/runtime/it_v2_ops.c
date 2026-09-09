#include <hpu/encoding.h>
#include <hpu/it_v2.h>
#include <klib.h>

/* 参数只选择已由 producer 编码的常量，不在 C 中拼位段或生成可执行内存。 */
#define ISSUE(word_) \
    __asm__ volatile(".word %0" : : "i"((uint32_t)(word_)) : "memory")
#define KEY(dst_, a_, b_) (((dst_) << 6U) | ((a_) << 3U) | (b_))
#define OBJECT_CASE(op_, dst_, a_, b_) \
    case KEY(dst_, a_, b_): \
        ISSUE(HPU_INSN_##op_##_P##dst_##_P##a_##_P##b_); return 0
#define OBJECT_CASES(op_) \
    OBJECT_CASE(op_, 2, 0, 1); \
    OBJECT_CASE(op_, 0, 0, 1); \
    OBJECT_CASE(op_, 1, 0, 1); \
    OBJECT_CASE(op_, 2, 1, 0); \
    OBJECT_CASE(op_, 7, 0, 1); \
    OBJECT_CASE(op_, 2, 2, 0); \
    OBJECT_CASE(op_, 2, 2, 1)

static int object_error(const char *op, unsigned dst, unsigned a, unsigned b) {
    printf("[HPU][FAIL][%s] unsupported dst=p%u a=p%u b=p%u; no instruction issued\n",
           op, dst, a, b);
    return 1;
}

int op_add(unsigned dst, unsigned a, unsigned b) {
    if (dst <= P7 && a <= P7 && b <= P7) {
        switch (KEY(dst, a, b)) { OBJECT_CASES(PADD); }
    }
    return object_error("padd", dst, a, b);
}

int op_sub(unsigned dst, unsigned a, unsigned b) {
    if (dst <= P7 && a <= P7 && b <= P7) {
        switch (KEY(dst, a, b)) { OBJECT_CASES(PSUB); }
    }
    return object_error("psub", dst, a, b);
}

int op_mul(unsigned dst, unsigned a, unsigned b) {
    if (dst <= P7 && a <= P7 && b <= P7) {
        switch (KEY(dst, a, b)) { OBJECT_CASES(PMUL); }
    }
    return object_error("pmul", dst, a, b);
}

int op_mac(unsigned dst, unsigned a, unsigned b) {
    if (dst <= P7 && a <= P7 && b <= P7) {
        switch (KEY(dst, a, b)) { OBJECT_CASES(PMAC); }
    }
    return object_error("pmac", dst, a, b);
}

#define IMMEDIATE_CASE(op_, dst_, imm_) \
    case imm_: ISSUE(HPU_INSN_##op_##_IMM##imm_##_P##dst_##_P0); return 0
#define IMMEDIATE_CASES(op_, dst_) \
    IMMEDIATE_CASE(op_, dst_, 0); \
    IMMEDIATE_CASE(op_, dst_, 1); \
    IMMEDIATE_CASE(op_, dst_, 7); \
    IMMEDIATE_CASE(op_, dst_, 255)

static int immediate_error(const char *op, unsigned dst, unsigned a, unsigned imm) {
    printf("[HPU][FAIL][%s] unsupported dst=p%u a=p%u imm=%u; no instruction issued\n",
           op, dst, a, imm);
    return 1;
}

int op_mul_imm(unsigned dst, unsigned a, unsigned imm) {
    if (a == P0 && dst == P2) {
        switch (imm) { IMMEDIATE_CASES(PMUL, 2); }
    } else if (a == P0 && dst == P0) {
        switch (imm) { IMMEDIATE_CASES(PMUL, 0); }
    }
    return immediate_error("pmul-imm", dst, a, imm);
}

int op_mac_imm(unsigned dst, unsigned a, unsigned imm) {
    if (a == P0 && dst == P2) {
        switch (imm) { IMMEDIATE_CASES(PMAC, 2); }
    } else if (a == P0 && dst == P0) {
        switch (imm) { IMMEDIATE_CASES(PMAC, 0); }
    }
    return immediate_error("pmac-imm", dst, a, imm);
}

#define STAGE_CASE(prefix_, n_) case n_: ISSUE(prefix_##n_); return 0
#define STAGE_CASES(prefix_) \
    STAGE_CASE(prefix_, 0); STAGE_CASE(prefix_, 1); STAGE_CASE(prefix_, 2); \
    STAGE_CASE(prefix_, 3); STAGE_CASE(prefix_, 4); STAGE_CASE(prefix_, 5); \
    STAGE_CASE(prefix_, 6); STAGE_CASE(prefix_, 7); STAGE_CASE(prefix_, 8); \
    STAGE_CASE(prefix_, 9); STAGE_CASE(prefix_, 10); STAGE_CASE(prefix_, 11)

static int stage_error(const char *op, unsigned data,
                       unsigned twiddle, unsigned stage) {
    printf("[HPU][FAIL][%s] unsupported data=p%u twiddle=p%u stage=%u; "
           "no instruction issued\n", op, data, twiddle, stage);
    return 1;
}

int op_ntt(unsigned data, unsigned twiddle, unsigned stage) {
    if (data == P0 && twiddle == P1) {
        switch (stage) { STAGE_CASES(HPU_INSN_PNTT_STAGE); }
    } else if (data == P2 && twiddle == P3) {
        switch (stage) { STAGE_CASES(HPU_INSN_PNTT_P2_P3_STAGE); }
    }
    return stage_error("pntt", data, twiddle, stage);
}

int op_intt(unsigned data, unsigned twiddle, unsigned stage) {
    if (data == P0 && twiddle == P1) {
        switch (stage) { STAGE_CASES(HPU_INSN_PINTT_STAGE); }
    } else if (data == P2 && twiddle == P3) {
        switch (stage) { STAGE_CASES(HPU_INSN_PINTT_P2_P3_STAGE); }
    }
    return stage_error("pintt", data, twiddle, stage);
}
