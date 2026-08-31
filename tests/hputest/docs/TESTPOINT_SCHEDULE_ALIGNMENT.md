# HPU 测试点、甘特图与冒烟用例对齐

本文固定 Nexus-AM HPU 测试源码、测试点文档和项目甘特图之间的命名关系。
机器可检查的唯一用例清单是 [`../cases.tsv`](../cases.tsv)。甘特图中的每一行都应
填写该表中的完整 `case_id` 和 `source`，不能再建立一套独立编号或用相近标题代替。

## 1. 测试类别

| 甘特图类别 | 源码目录 | 测试点数 | 测试点 ID |
|---|---|---:|---|
| 配置测试 | `src/01_configuration` | 7 | `HPU_IT_DIR_CFG_001..006`、`HPU_IT_STING_CFG_001` |
| 数据通路测试 | `src/02_data_paths` | 6 | `HPU_IT_DIR_PATH_001..004`、`HPU_IT_STING_CMD_001`、`HPU_IT_STING_PATH_005` |
| 指令测试 | `src/03_compute_instructions` | 9 | `HPU_IT_DIR_INS_C0_001..009` |
| 算子/组合序列测试 | `src/04_composite_instruction_sequences` | 13 | `HPU_IT_DIR_CMB_001..005`、`HPU_IT_DIR_CMB_009..015`、`HPU_IT_STING_CMB_007` |
| 结构/互联测试 | `src/05_cpu_hpu_structural_connectivity` | 7 | `HPU_IT_DIR_STR_001..004/006`、`HPU_IT_STING_STR_001/005` |
| 性能测试 | `src/06_performance` | 6 | `HPU_IT_DIR_PERF_001..006` |
| 应用测试 | `src/07_full_application` | 1 | `HPU_IT_DIR_APP_001` |

以上共 49 个迁移测试点。`src/00_bringup` 下的 7 个冒烟用例和 2 个
`main()` 返回值探针是上板/仿真辅助程序，不应在甘特图中重复计为这 49 个 IT
测试点。测试是否已经具备真实指令和 self-check，还必须同时查看 `cases.tsv` 的
`qualifier`；`blocked-not-issued` 不能因为排进甘特图就改写成已完成。

## 2. 001 冒烟用例的最终编号

当前 HPU 配置和状态寄存器只有地址 `0x08000000..0x0800001c` 的 MMIO
访问方式。没有已定义的 HPU architectural CSR 编号，因此不提供 `csrr HPU_STATUS`
版本，也不能拿 `sip`、`mip` 等标准中断 CSR 冒充 HPU STATUS。

| 编号 | 文件 | 唯一目的 |
|---:|---|---|
| 01 | `01_dload_hold.c` | DLOAD 后永久等待，给波形观察留下时间 |
| 02 | `02_read_write_mmio_csr.c` | 逐地址写读 BASE/SIZE，COMMIT 后检查 MMIO STATUS |
| 03 | `03_dload_poll_mmio.c` | DLOAD，轮询 MMIO STATUS 的 busy 由 0→1→0 |
| 04 | `04_psync_irq.c` | 空闲 PSYNC，以 PLIC source 257 中断完成 |
| 05 | `05_dload_psync_irq.c` | DLOAD+PSYNC，以 PLIC 中断和 `volatile` flag 完成 |
| 06 | `06_dload_dstore_poll_mmio.c` | DLOAD+DSTORE+PSYNC，以 MMIO 轮询完成并逐 4096 系数比对 |
| 07 | `07_dload_dstore_psync_irq.c` | 同一回环数据流，以 PSYNC 中断完成并逐 4096 系数比对 |
| 08 | `08_dload_compute_dstore_psync_irq.c` | producer PMUL 程序，以 PSYNC 中断完成并逐 4096 系数与 golden/C oracle 比对 |

当前编号只保留 MMIO 状态访问方式：

- 03 只验证 DLOAD 的 MMIO busy 轮询；
- 04 单独隔离空闲 PSYNC 的中断路径；
- 05 在 DLOAD 后验证 PSYNC 中断；
- 06 与 07 使用完全相同的数据回环，仅 CPU 同步方式分别是 MMIO 和中断；
- 08 增加 producer 生成的 PMUL 计算闭环。

中断用例公共层使用 `csrs/csrc sie`，只是在 S-mode 打开/关闭标准外部中断，
不是第二种 HPU 状态寄存器访问方式。

## 3. IT 回传失败记录的解释边界

以下内容来自 IT/VCS 人工回传的日志和波形，不是 GitHub Actions 或本仓库本地
执行出的 VCS 结果；测试源码不得为了绕过这些现象而放宽判据。

- 06 在旧 RTL 上报告 DSTORE 倒数 burst 附近等待不到 `dma_ack`，随后 DMA
  timeout。该结果应保留为 RTL/版本复验项；增加 C 侧等待次数不能修复 RTL 内部
  ACK 超时。
- 05 卡住时，应先确认 DLOAD 是否完成、PSYNC 是否入队、HPU IRQ 电平是否置位、
  PLIC source 257 是否被 claim。中断用例不会用 UART 文本作为完成证据。
- 07 的 SRAM 警告中 `X` 表示 unknown，`Z` 表示 high impedance；这是 Verilog
  四态数据，不是 RISC-V 的 `Z*` 指令集扩展。应在同一拍区分
  `wr_dma_valid/wr_dma_data` 与计算写回 `wr0_valid/wr0_data`、
  `wr1_valid/wr1_data`，再看最终选择信号和 bank write-enable。
- C self-check 可以在 X/Z 污染落入 DDR 结果后通过 `return 1` 报错，但 C 的
  二态整数本身不能替代波形对 X/Z 来源的定位。

重新运行时必须记录实际 RTL commit。若使用的是聊天中确认的旧 RTL，05/06/07
结果只能作为旧版本失败记录，不能据此修改 Nexus-AM 测试去“适配”错误实现。
