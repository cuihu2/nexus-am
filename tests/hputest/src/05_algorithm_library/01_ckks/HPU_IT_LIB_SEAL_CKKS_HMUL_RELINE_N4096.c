#include <hpu/application_case.h>
#include <hpu/completion.h>
#include <hpu/log.h>
#include <hpu/progress.h>
#include <hpu/report.h>
#include <hpu/result.h>
#include <hpu/trace.h>
#include <seal_ckks_hmul_n4096.h>

/*
 * 测试点：HPU_IT_LIB_SEAL_CKKS_HMUL_RELINE_N4096
 * 目的：验证modified-SEAL库接口CKKS，N=4096。
 * 密文乘法后重线性化；分别核对三分量乘积与两分量最终结果。
 * 真实调用inline-asm内置modified-SEAL的Evaluator，软件模型与目标均逐word自检。
 * inline-asm生成数据和指令；目标核执行指令流，不直接链接主机C++库。
 * 每个ELF只测一次，DSTORE之后仅发一次PSYNC；默认少打印，支持silent。
 */
int main(void) {
    int rc;
    case_start(__FILE__);
    (void)result_context(__FILE__, 0U);
    if (progress_begin(__FILE__, 1U) != 0) return case_fail(__FILE__, __LINE__);
    trace_phase(TRACE_PREPARE);
    phase_mark("prepare");
    /* 拷贝本用例的密文、密钥和模表；输出预置毒值，防止漏写误通过。 */
    if (application_prepare() != 0) return case_fail(__FILE__, __LINE__);

    trace_phase(TRACE_CONFIGURE);
    phase_mark("configure");
    /* 写shadow配置并回读，再COMMIT；寄存器级步骤保留在用例中。 */
    csr_write(CSR_FAULT, FAULT_VALID);
    csr_write(CSR_IRQ, IRQ_LEVEL);
    csr_write(CSR_IRQ, 0U);
    csr_write(CSR_BASE_LO, (uint32_t)MEM_BASE);
    csr_write(CSR_BASE_HI, (uint32_t)(MEM_BASE >> 32U));
    csr_write(CSR_SIZE_LO, HPU_APPLICATION_CAPACITY_LINES);
    csr_write(CSR_SIZE_HI, 0U);
    if (expect_csr(CSR_BASE_LO, (uint32_t)MEM_BASE, UINT32_MAX) != 0 ||
        expect_csr(CSR_BASE_HI, (uint32_t)(MEM_BASE >> 32U), UINT32_MAX) != 0 ||
        expect_csr(CSR_SIZE_LO, HPU_APPLICATION_CAPACITY_LINES, UINT32_MAX) != 0 ||
        expect_csr(CSR_SIZE_HI, 0U, UINT32_MAX) != 0)
        return case_fail(__FILE__, __LINE__);
    csr_write(CSR_COMMIT, COMMIT);
    if (wait_window(1) != 0 || check_status() != 0) return case_fail(__FILE__, __LINE__);

    trace_phase(TRACE_ISSUE);
    phase_mark("issue");
    /* 指令发射及DMA位置可从application_trace定位，不逐系数打印。 */
    rc = hpu_run_seal_ckks_hmul_n4096();
    if (rc != 0) {
        LOG_ERROR("[HPU][SEAL][PROGRAM-FAIL] rc=%d instruction=%u dma=%u\n",
                  rc, application_trace.instruction, application_trace.dma);
        return case_fail(__FILE__, __LINE__);
    }
    trace_phase(TRACE_WAIT);
    phase_mark("wait-completion");
    /* MMIO等完成电平和空闲同时可见；不启用PLIC或计时器中断。 */
    if (application_wait() != 0 || completion_clear() != 0 || check_status() != 0)
        return case_fail(__FILE__, __LINE__);

    trace_phase(TRACE_COMPARE);
    phase_mark("compare-results");
    const int data_rc = application_check_results();
    trace_phase(TRACE_GUARD);
    phase_mark("check-guard");
    const int guard_rc = application_check_memory();
    if (data_rc != 0 || guard_rc != 0) return case_fail(__FILE__, __LINE__);
    trace_phase(TRACE_DONE);
    phase_mark("case-done");
    return case_pass(__FILE__);
}
