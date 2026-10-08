#include <hpu/log.h>
#include <hpu/result.h>

/*
 * 测试点：HPU_IT_EDGE_WATCHDOG_PENDING
 * 目的：记录新增硬件看门狗的验证缺口，不把软件等待预算冒充硬件测试。
 * 待确认设计版本、触发/复位方式、阈值单位、可见fault/IRQ和故障恢复。
 * 无接口契约时不发非法请求、不编造CSR或超时数字，不发布占位ELF。
 */
int main(void) {
    case_start(__FILE__);
    LOG_ERROR("[HPU][BLOCKED] watchdog RTL/interface contract not supplied\n");
    case_not_qualified(__FILE__);
    return 1;
}
