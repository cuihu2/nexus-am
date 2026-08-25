#include <hpu/it_case_steps.h>

#define CASE_ID "HPU_IT_DIR_CMB_005"
#define TESTPOINT "IT-CMB-005"
#define DESCRIPTION "NTT与Auto联合序列待运行时绑定用例"
#define TEST_MODE "定向组合算子待接入"
#define PRIORITY 2
#define CASE_KIND "HPU_CASE_CMB_NTT_AUTO"
#define REQUIREMENTS "HPU_REQ_IT_MONITOR | HPU_REQ_CACHE_CONTRACT | HPU_REQ_EXTERNAL_DATA | HPU_REQ_EXTERNAL_ENTRY"
#define SEED 0u

int main(void) {
    /*
     * 当前边界 / current boundary:
     * 1. CSR：本用例尚未获得完整算法的窗口布局和寄存器配置契约。
     * 2. 数据：缺少经确认的 producer fixture/golden 或外部数据绑定。
     * 3. 指令：不发出任何 HPU 命令；不能用其他算子冒充“执行 NTT 与 Auto 联合序列”。
     * 4. 同步：没有真实命令，因此不伪造 PSYNC 完成。
     * 5. 比对：没有可信输出可比对，明确 return 1，防止 VCS 报出假 PASS。
     * 外部证据边界：return 0 只表示软件自检通过；IT monitor、外部数据、外部入口 证据仍由 IT/VCS 环境判定。
     */
    (void)hpu_it_not_issued();
    return 1;
}
