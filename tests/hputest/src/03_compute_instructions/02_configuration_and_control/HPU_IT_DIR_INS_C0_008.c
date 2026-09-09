#include <hpu/irq.h>
#include <hpu/it_v2.h>
#include <hpu/result.h>

/*
 * 测试点：IT-INS-C0-008
 * 目的：PSYNC 的空闲、DMA 前序、计算前序及连续两轮中断。
 * 每个场景的两轮是两段完整程序，各自在末尾发一次 PSYNC；第二轮使用 irq_rearm。
 * 软件不保证 CPU 发出 PSYNC 的瞬间 DMA/计算仍忙，精确完成时序由 IT 波形核对。
 * 复用冒烟 PLIC/ISR：claim source 257、清 HPU IRQ、complete，且不开定时器中断。
 */
int main(void) {
    static uint32_t golden[POLY_WORDS];
    const char *const names[3] = {"idle", "dma", "compute"};
    case_start(__FILE__);

    for (unsigned scenario = 0U; scenario < 3U; ++scenario) {
        for (unsigned round = 0U; round < 2U; ++round) {
            const uint32_t *a;
            const uint32_t *b;
            uint32_t status = 0U;
            uint32_t fault = 0U;
            uint32_t level;
            unsigned polls;
            int rc;

            printf("[HPU][PSYNC][ROUND] scenario=%s round=%u profile=%u "
                   "completion=PLIC-source-257 words=%u\n",
                   names[scenario], round, round, POLY_WORDS);
            if (v2_prepare(round, MOD_Q0, MOD_Q1) != 0)
                return case_fail(__FILE__, __LINE__);
            a = v2_expected(LINE_A);
            b = v2_expected(LINE_B);
            if (scenario != 0U) {
                if (v2_allow_output(LINE_OUT, POLY_LINES) != 0)
                    return case_fail(__FILE__, __LINE__);
                for (unsigned word = 0U; word < POLY_WORDS; ++word) {
                    golden[word] = scenario == 1U ? a[word] :
                        (uint32_t)(((uint64_t)a[word] * b[word]) % MOD_Q0);
                }
            }
            /* 配置窗口逐寄存器写入、读回；COMMIT 才使这一组 BASE/SIZE 生效。 */
            csr_write(CSR_FAULT, FAULT_VALID);
            csr_write(CSR_IRQ, IRQ_LEVEL);
            csr_write(CSR_IRQ, 0U);
            csr_write(CSR_BASE_LO, (uint32_t)MEM_BASE);
            csr_write(CSR_BASE_HI, (uint32_t)(MEM_BASE >> 32U));
            csr_write(CSR_SIZE_LO, WINDOW_LINES);
            csr_write(CSR_SIZE_HI, 0U);
            if (expect_csr(CSR_BASE_LO, (uint32_t)MEM_BASE, UINT32_MAX) != 0)
                return case_fail(__FILE__, __LINE__);
            if (expect_csr(CSR_BASE_HI, (uint32_t)(MEM_BASE >> 32U),
                           UINT32_MAX) != 0)
                return case_fail(__FILE__, __LINE__);
            if (expect_csr(CSR_SIZE_LO, WINDOW_LINES, UINT32_MAX) != 0)
                return case_fail(__FILE__, __LINE__);
            if (expect_csr(CSR_SIZE_HI, 0U, UINT32_MAX) != 0)
                return case_fail(__FILE__, __LINE__);
            csr_write(CSR_COMMIT, COMMIT);
            if (wait_window(1) != 0 || check_status() != 0)
                return case_fail(__FILE__, __LINE__);

            rc = round == 0U ? irq_open() : irq_rearm();
            if (rc != 0) {
                irq_close();
                printf("[HPU][PSYNC][FAIL] phase=%s scenario=%s round=%u rc=%d\n",
                       round == 0U ? "irq-open" : "irq-rearm",
                       names[scenario], round, rc);
                return case_fail(__FILE__, __LINE__);
            }

            printf("[HPU][PSYNC][ISSUE] scenario=%s round=%u "
                   "terminal-PSYNC-count=1; no polling/printing inside command burst\n",
                   names[scenario], round);
            if (scenario == 1U) {
                /* DMA 前序：数据回环后，PSYNC 等待包含 DSTORE 在内的前序命令。 */
                if (dload(P0, LINE_A, POLY_LINES) != 0 ||
                    dstore_release(P0, LINE_OUT, POLY_LINES) != 0) {
                    irq_close();
                    printf("[HPU][PSYNC][FAIL] phase=issue-dma\n");
                    return case_fail(__FILE__, __LINE__);
                }
            } else if (scenario == 2U) {
                /* 计算前序：保留完整计算和写回，不在 PMUL/DSTORE 之间同步。 */
                if (dload_mod(LINE_MOD, 1U) != 0 || pmodld(0U) != 0 ||
                    dload(P0, LINE_A, POLY_LINES) != 0 ||
                    dload(P1, LINE_B, POLY_LINES) != 0 ||
                    op_mul(P2, P0, P1) != 0 ||
                    dstore_release(P2, LINE_OUT, POLY_LINES) != 0 ||
                    pfree(P0) != 0 || pfree(P1) != 0 || pfree(P4) != 0) {
                    irq_close();
                    printf("[HPU][PSYNC][FAIL] phase=issue-compute\n");
                    return case_fail(__FILE__, __LINE__);
                }
            }
            psync();
            rc = irq_wait();
            if (rc != 0) {
                irq_close();
                printf("[HPU][PSYNC][FAIL] phase=irq-wait scenario=%s "
                       "round=%u rc=%d\n", names[scenario], round, rc);
                return case_fail(__FILE__, __LINE__);
            }

            /* ISR 的完成通知与 BUSY 状态存在跨时钟域差异，通知后继续确认真正空闲。 */
            for (polls = 0U; polls < TIMEOUT; ++polls) {
                status = csr_read(CSR_STATUS);
                fault = csr_read(CSR_FAULT);
                if ((status & STATUS_FAULT) != 0U ||
                    (fault & FAULT_VALID) != 0U) {
                    irq_close();
                    printf("[HPU][PSYNC][FAIL] phase=final-status "
                           "status=0x%x fault=0x%x polls=%u\n", status, fault, polls);
                    return case_fail(__FILE__, __LINE__);
                }
                if ((status & (STATUS_VALID | STATUS_BUSY)) == STATUS_VALID)
                    break;
            }
            if (polls == TIMEOUT) {
                irq_close();
                printf("[HPU][PSYNC][FAIL] phase=idle-timeout "
                       "status=0x%x fault=0x%x polls=%u\n", status, fault, polls);
                return case_fail(__FILE__, __LINE__);
            }
            level = csr_read(CSR_IRQ);
            if ((level & IRQ_LEVEL) != 0U) {
                irq_close();
                printf("[HPU][PSYNC][FAIL] phase=isr-clear irq=0x%x expected=0\n",
                       level);
                return case_fail(__FILE__, __LINE__);
            }
            rc = v2_check_memory("PSYNC-readonly-and-guard");
            if (rc == 0 && scenario != 0U)
                rc = v2_check_words(names[scenario], LINE_OUT, golden,
                                    POLY_WORDS, MOD_Q0);
            if (rc != 0) {
                irq_close();
                printf("[HPU][PSYNC][FAIL] phase=data-check scenario=%s round=%u rc=%d\n",
                       names[scenario], round, rc);
                return case_fail(__FILE__, __LINE__);
            }
            printf("[HPU][PSYNC][ROUND-PASS] scenario=%s round=%u "
                   "irq-cleared=1 status=0x%x readonly-and-guard=pass\n",
                   names[scenario], round, status);
        }
        irq_close();
    }
    return case_pass(__FILE__);
}
