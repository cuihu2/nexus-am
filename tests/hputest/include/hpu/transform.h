#ifndef HPU_TRANSFORM_H
#define HPU_TRANSFORM_H

#include <hpu/steps.h>

/* 512 line 放不下完整 12 级表；独立算子用 640 line，不改变冒烟窗口。 */
enum {
    TRANSFORM_LINES = 640,
    TRANSFORM_WORDS = TRANSFORM_LINES * WORDS_PER_LINE,
    TRANSFORM_N = 4096,
    TRANSFORM_Q = 50061313,
    TRANSFORM_INPUT = 1,
    TRANSFORM_OUTPUT = 65,
    TRANSFORM_MOD = 129,
    TRANSFORM_FACTOR = 130,
    TRANSFORM_STAGE_BASE = 194
};

struct transform_binding {
    unsigned instruction;
    unsigned line;
    unsigned lines;
    const char *operation;
    const char *artifact;
};

/* 仅复制/核对 DDR，不配置 CSR、不发 HPU 指令、不隐藏同步。 */
int transform_prepare(const uint32_t *image);
int transform_check_memory(const uint32_t *image);
int transform_check_result(const uint32_t *golden);
void transform_print_bindings(const struct transform_binding *bindings, unsigned count);

#endif
