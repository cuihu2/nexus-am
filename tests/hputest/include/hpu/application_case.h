#ifndef HPU_APPLICATION_CASE_H
#define HPU_APPLICATION_CASE_H

#include <application_layout.h>
#include <hpu/steps.h>

extern const uint32_t application_window[HPU_APPLICATION_LINES * WORDS_PER_LINE];
extern const uint32_t application_golden[HPU_APPLICATION_GOLDEN_WORDS];
extern const uint8_t application_writable[HPU_APPLICATION_LINES];
int application_prepare(void);
int application_check_results(void);
int application_check_memory(void);
/* 按模拟 cycle 限制完成轮询，不把 TIMEOUT 次 MMIO 当作 TIMEOUT 个 cycle。 */
int application_wait(void);

#endif
