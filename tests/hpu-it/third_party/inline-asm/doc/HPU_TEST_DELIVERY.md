# HPU 指令流与验证数据交付说明

## 1. 交付结论

本仓库已形成可复现的软件交付闭环：生成 HPU 算子指令流、编码为 32-bit RV 指令和 26-bit HPU 命令、生成完整密文乘法及重线性化的 RNS golden 数据、执行软件解密校验，并生成 RV 接口冒烟用例。

一键生成和验收：

```bash
cmake -S . -B build
cmake --build build -j --target hpu_delivery
ctest --test-dir build --output-on-failure
```

成功后，`outputs/DELIVERY_REPORT.txt` 中应包含：

```text
SOFTWARE_DELIVERY=PASS
FHE_REFERENCE=PASS
ASM_ENCODING=PASS
PRECODE_CMD26=PASS
MOD_CTX_SMALL_BANK_FLAG=PASS
INSTRUCTION_SET_11=PASS
PFREE_LIFECYCLE=PASS
RV_INTERFACE_SMOKE=PASS
OPERATOR_UT_PACKAGES=PASS
HARDWARE_UINT32_IMAGES=PASS
HPU_LINE_LAYOUT_256B=PASS
CUSTOM1_LINE_SIDEBAND=PASS
HPU_MEM_CSR_MAP=PASS
MOD_CTX_Q32_MU48=PASS
MOD_TABLE_BASE_0X1400=PASS
STAGE_TWIDDLE_LAYOUT=PASS
NEGACYCLIC_FACTORS_EXPLICIT=PASS
NTT_PHYSICAL_OUT_OF_PLACE=PASS
HARDWARE_EXECUTION=CONDITIONAL
```

`HARDWARE_EXECUTION=CONDITIONAL` 不是算法或数据失败。`uint32` 镜像、256B line、`mod_ctx`、twiddle、`.cmd26`、custom1 sideband、类型化 DMA span 和 CSR 偏移均已生成或冻结；生成的 C 后端在每条 DMA 前绑定 `x10/x11`。Nexus-AM IT runtime 已解析实际 line offset/count、scratch、cache、FAULT/IRQ 和 HPU_MEM window。这里剩余的条件是目标 RTL/板级执行及 monitor 证据，而不是软件 DMA 占位。

## 2. 程序入口与执行顺序

项目包含三个独立可执行入口，不存在 `test/reference/main.cpp` 替代 `src/main.cpp` 的关系：

| 顺序 | 源入口 | 可执行文件 | 职责 |
| --- | --- | --- | --- |
| 1 | `src/main.cpp` | `inline_asm_codegen` | 生成 `output/*.cpp` 和 `output/*.asm` 指令流 |
| 2 | `test/encode/main.cpp` | `inline_asm_encode_outputs` | 归档到 `outputs/`、编码 `.inst32`/`.cmd26`、生成 RV 冒烟流 |
| 3 | `test/reference/main.cpp` | `hpu_reference_vectors` | 生成并校验完整乘法 golden，拆分各算子 UT 数据 |

顶层 `hpu_delivery` 依次执行三个程序，再调用 `test/delivery/check_delivery.cmake` 检查文件完整性、FHE 校验结果、阶段标记和指令数量。单独执行 `hpu_reference_vectors` 只会生成数据，不会生成或更新 HPU ASM。

当前存在两组需要保持一致的源配置：

- `src/main.cpp`：HPU 指令生成参数，完整乘法、Relinearization 和 KeySwitch 复用 `kCiphertextMultiplyCfg`。
- `test/reference/main.cpp`：软件 reference 参数 `kN/kNumQ/kNumP/kDnum/kPlainModulus/kSeed`。

`outputs/*/test_data/params.json` 是 reference 写出的结果清单，不是配置入口。修改它不会影响生成逻辑，并会在下一次执行 `hpu_delivery` 时被覆盖。

## 3. FHE 算法流程

Golden 严格执行以下顺序：

```text
Encrypt(ctA, ctB)
  -> PMUL psi^i, then staged NTT(ctA[0], ctA[1], ctB[0], ctB[1]) over Q
  -> TensorProduct(t0, t1, t2) over Q
  -> staged INTT(t0, t1, t2), then PMUL N^-1*psi^-i
  -> Decompose t2 into Q digits
  -> ModUp each digit to full Q union P
  -> PMUL psi^i, then staged NTT over Q union P
  -> Multiply-accumulate with rlk[digit][0..1]
  -> staged INTT over Q union P, then PMUL N^-1*psi^-i
  -> ModDown P from both key-switch components
  -> (out0, out1) = (t0 + ks0, t1 + ks1)
  -> Decrypt and compare with mA * mB in Z_t[x]/(x^N+1)
```

当前参数与主指令流一致：`N=4096`、`num_q=4`、`num_p=3`、`dnum=2`。数据使用确定性、无噪声、P 可整除的功能测试评估密钥，以获得逐位可比结果；它用于 UT/IT 定位，不代表生产密钥安全性。

生成器和 reference 共同检查 `N` 为 2 的幂且 `ceil(N/64) <= 1024`，对应当前普通 bank 的最大可承载次数 `N=65536`。`dload load_type` 只接受 `0=seg`、`1=poly`、`2=mod_ctx`；编码值 3 为保留值并纳入 RV 负例。

## 4. 数据格式

完整乘法数据位于 `outputs/ciphertext_multiply/test_data/`：

| 文件 | 用途 |
| --- | --- |
| `params.json` | 模数、根、环参数、NTT 约定和安全属性 |
| `artifact_manifest.csv` | 每个二进制及可读文本的路径、shape、字节数和 FNV-1a 校验值 |
| `memory_map.json` | 指向 `uint32` HPU_MEM 镜像、256B line map 和剩余联调字段 |
| `dma_plan.csv` | 各算法阶段的输入、输出、域和基顺序 |
| `input/*.bin` | 两个输入密文、测试明文和测试私钥 |
| `constants/*.bin` | `rlk[digit][component][basis][coefficient]` |
| `expected/*.bin` | NTT、tensor、ModUp、KeySwitch、ModDown 和最终结果检查点 |
| `VALIDATION.txt` | 软件参考模型最终校验结果 |

顶层 `.bin` 均采用 little-endian `uint64_t` canonical residue，只作为数学 golden。多维数组按 C row-major 展平，最后一维始终是 coefficient；基顺序固定为 `Q[0..3]` 后接 `P[0..2]`。

每个 `.bin` 都有同名 `.hex.txt` 人工可读版本，例如 `input.bin` 对应 `input.hex.txt`。文本文件头包含用途、shape、维度含义和编码说明，多维数据按 component/digit/basis 分块，并在每行标注 coefficient 范围。

每个完整乘法包和独立 UT 包还包含 `hardware/`：

| 文件 | 用途 |
| --- | --- |
| `hpu_mem_image.u32.bin` | 可整体装入 HPU_MEM window 的连续 `uint32` 镜像 |
| `images/**/*.u32.bin` | 输入、常量、期望结果的独立 256B-line-padded 镜像 |
| `line_map.csv` | 每个对象的 byte address、line offset、line count、payload/padded 大小 |
| `constants/mod_ctx.u32.bin` / `mod_ctx_map.csv` | 每个 Q/P 模数的 q 与 `floor(2^64/q)` Barrett mu 物理记录 |
| `constants/twiddle/**/*.u32.bin` / `twiddle_map.csv` | 每个 basis、方向、phase、stage 的物理 twiddle 和 line 位置 |
| `hpu_mem_config.json` | HPU_MEM base/size、256B line 参数、`0x00..0x18` CSR 偏移和编程顺序 |
| `abi.json` | `uint32`、小端、Bank 5、mod context word 布局和 NTT/INTT twiddle 约定 |

硬件模上下文 V1 每条记录占 128 bit，按低位到高位为
`{q[31:0], mu[47:0], reserved[47:0]}`，其中
`mu=floor(2^64/q)`，且 `65537 <= q <= 2^32-1`。按 `uint32` word
查看时是 `q`、`mu[31:0]`、`{16'b0,mu[47:32]}`、全零保留字。模表通过
`dload type=2, flag[0]=1` 请求分配到 small Bank 5，DMA 与后续访问的
一致性由硬件维护，软件可直接由 `pmodld MOD_ID` 选择表项。Bank 5 固定为
`0x1400..0x141F` 共 32 line，物理可放 512 条记录；但 `MOD_ID` 为 8 bit，
所以软件只允许 256 个 context，寻址 `0x1400..0x140F`。

Twiddle 与软件 reference 一致，采用 negacyclic pre-twist 和 radix-2 DIT；
NTT stage 0 前显式执行 `PMUL psi^i`，INTT 最后一个 stage 后显式执行
`PMUL (N^-1 * psi^-i)`，不依赖 PE 隐式归一化或 twist。每个 stage 按
group-major butterfly 顺序展开为固定 `N/2` 个 `uint32`，即 `N/128` 条
256B line。默认 `N=4096` 时每个 stage 恰为 2048 words、32 line，不再使用
“唯一 twiddle 由各 group 复用”的压缩镜像。

## 5. 失败定位

| 首个失败检查点 | 优先排查模块 |
| --- | --- |
| `inputs_ntt_q` | NTT、twiddle、模上下文、数据排列 |
| `tensor_ntt_q` | PE 的 `pmul/pmac`、活动模上下文 |
| `tensor_coeff_q` | INTT、归一化因子、输出排列 |
| `modup_t2_coeff_qp` | BConv 常数、digit 偏移、Q/P context 编号 |
| `keyswitch_accum_ntt_qp` | rlk 布局、digit/component/basis 步长、PMAC 累加 |
| `keyswitch_moddown_q` | P->Q BConv、`P^-1 mod q_i`、减法方向 |
| `ciphertext_out_q` | 最终 `padd`、输出 component 顺序 |
| 最终解密 | 上述节点均通过时再检查方案参数和 host 数据解释 |

同一 reference 还会拆分到 `outputs/{ntt,intt,mm,bconv,modup,pmult,cmult,moddown,keyswitch,relinearization}/test_data/`。每个目录均包含独立 `params.json`、数学输入/期望输出、checksum，以及完整的 `hardware/` 镜像、上下文、twiddle 和 line map，可直接交给对应模块负责人跑 UT。`auto` 当前无法完成物理寄存器编码，其阻塞原因记录在 `outputs/auto/test_data/STATUS.md`。

## 6. RV 接口用例

`outputs/rv_interface_smoke/` 包含：

- `rv_interface_smoke.asm`：覆盖 11 条体系结构指令、四种 DLoad type、DStore retain/release 和最大合法字段。
- `rv_interface_smoke.inst32`：对应 32-bit 指令流。
- `rv_interface_smoke.cmd26`：对应控制逻辑的 26-bit 命令流。
- `test_data/expected_decode.csv`：逐条期望 word、command26、`custom0/custom1` 路由和归一化汇编。
- `test_data/expected_cmd26.csv`：逐条验证 `cmd26[25]=custom_kind`、custom0 payload 直通和 custom1 语义字段重排。
- `test_data/negative_cases.asm.txt`：包含越界用例，以及必须拒绝的旧 `pshcfg/pshuf/pseed/psample` 助记符。

建议 RV 接口 IT 依次验证 decode 路由、队列 backpressure、顺序发射、`dload -> pmodld -> compute` 的硬件一致性、`dload -> compute -> pfree/dstore rel=1` ownership，以及末尾 `psync` 的 CPU 完成通知。`pfree` 必须在目标对象最后一次使用后生效；已经由 `dstore rel=1` 释放的对象不得重复释放。

## 7. 硬件联调前置确认

以下前四项仍需由控制逻辑、SRAM、DMA 和软件负责人共同签字确认，确认后才能把当前交付从“软件可验收”升级为“硬件可直接运行”。第 5 项记录硬件负责人已经确认的 `psync` 与 DMA 一致性口径：

1. runtime 按已冻结的 `GPR[rs1]=line_offset`、`GPR[rs2]=line_count` 语义，将 `line_map.csv` 的具体值绑定到每条 DMA 指令。
2. RTL 接受 V1 `mod_ctx = {reserved48, mu48, q32}`、32-line Bank 5 与固定 `MOD_TABLE_BASE_LINE=0x1400`。
3. RTL 的 `pntt/pintt stage` 接受 `twiddle_map.csv` 的 `N/2` group-major DIT 次序、物理 out-of-place 提交，以及显式 pre/post PMUL；当前数据为 canonical residue，不是 Montgomery 域。
4. ct、tensor、ModUp、rlk、KeySwitch scratch 的物理地址，以及 RTL 对 `pfree` 和 `dstore rel=1` 生命周期语义的实现。
5. `psync` 只放在整个程序的最后，用于向 CPU 报告程序完成；DMA 一致性由硬件维护，算子内部不插入 `psync` 等待 DMA。
