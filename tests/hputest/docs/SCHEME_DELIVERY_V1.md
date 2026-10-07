# main v1 交付与算法用例

当前章节已重分：04只放基本算子和指令链，库接口在05。本文以下使用原测试点编号描述实现，
旧CMB009/010/012/013/014的直接映射见`../case-aliases.tsv`与`../layer-migration.tsv`；不再重复发布相同程序。

固定 inline-asm main：`c11dfe2cfbe731d7b45aea0a62d993d733e1a089`。
依据 `doc/delivery/HPU_APPLICATION_PACKAGE_V1.md` 与 `IT_HANDOFF_CHECKLIST.md`。
本版本有八个默认应用，本次全部接入。章节结构和统一下载 workflow 保持原结构。
`scheme-cases.tsv` 列出程序、算法、规模及用途，`case-aliases.tsv` 给出旧编号映射。

## 三算法差异

| 用例族 | CKKS | BFV | BGV |
|---|---|---|---|
| CMB004 KeySwitch | singleton-Q digit，rounded-P ModDown | 系数域 KeySwitch，rounded-P ModDown | NTT 域，模 t special-P ModDown |
| CMB012 Reline | 三分量 NTT → 两分量 NTT | 三分量系数 → 两分量系数 | 三分量 NTT → 两分量 NTT，保留 correction factor |
| CMB013 降层 | rounded Rescale，更新 scale | rounded ModSwitch | 模 t 修正，更新 correction factor |
| CMB014 旋转 | slot 左旋及 Galois KeySwitch | row 左旋及 KeySwitch | row 左旋及模 t KeySwitch |

CMB009 HADD、CMB010 HMUL也分别生成三算法版本。HMUL包含必要的重线性化，
最终两分量，不隐式降层。CMB004把tensor的c0/c1置零，隔离c2的KeySwitch修正；
CMB012保留真实乘法tensor的c0/c1，验证最终合并，输入及golden不同。

六个用例族主规模均为N4096，另有三个独立`CMB_004_*_KEYSWITCH_N128`定位项。
每个ELF只运行一遍。N128通过不能代替N4096通过。
例如`HPU_IT_DIR_CMB_004_BGV_KEYSWITCH_N4096`、
`HPU_IT_DIR_CMB_013_CKKS_RESCALE_N4096`，源文件与ELF/BIN/TXT同名。

## Golden与门禁

1. 独立modified-SEAL Evaluator生成逐算子ciphertext，转换成包内HPU golden。
2. 上游软件执行器逐字对拍SEAL；两个required oracle检查必须pass。
3. AM复放实际ASM、resolved DMA、模表、11条指令的算术及对象生命周期，
   检查逐算子golden及未写入区/guard。
4. 从编译后的ELF反汇编和只读符号核对实际指令、输入、golden与权限掩码。

复放器的NTT/PINTT stage已对照生产者硬件模型验证N128/N4096/N65536。
它不模拟AXI时序、allocator延迟、CDC或CPU前端，不能代替IT/VCS。
原包保留在每例`upstream/`，原oracle的`rtl_verified=false`保持不变。
`PROGRAM_MODEL.log`、`AM_ADAPTATION.json`单独记录AM的接收和适配结果。

## 已复现的三处交付差异

### CKKS P ModDown未取整

当前main的CKKS KeySwitch指令展开使用通用未取整ModDown，
SEAL与CkksSoftwareExecutor却使用rounded-P。N4096 Reline的原始指令复放
八个RNS limb全部失配，高层软件模型检查仍会通过。

补齐方案在系数accumulator的Q/P residues中加`floor(P/2)`，利用
`round(x/P) = ModDown(x + floor(P/2), P)`。
只使用现有模加、减、乘，无须系数比较指令。新指令仍由同一main编码器生成，
重新绑定DMA和C/H/INST32/CMD26。补齐后逐指令结果与SEAL golden精确一致。
原始包不改写，适配单独记录。

### BFV自然系数与物理顺序

上游BFV ciphertext/plaintext/golden是自然系数顺序，实际HPU NTT memory
约定要求位反转系数顺序。AM对系数输入与SEAL系数golden做相同的双射排列；
key、NTT/twiddle不重新排列，residue数值不改。适配后BFV Reline逐字对上。

### CKKS中间输出被原地覆盖

Relinearize/Rescale会把原NTT输入转为系数并原地写回。输入若是前一节点的output，
最后再比前一节点的NTT golden就会失败，后续分支复用也可能受影响。
AM保持首次INTT读原NTT，把系数写回及后续读取重定位到本节点的私有workspace。
原输入/输出得以保留，不增加变换指令，不删除中间golden检查。

## 不取整精度评估

本次最终选择：保留当前应用包/SEAL精确golden，补齐纯模运算取整。
下列未取整结果仅用于诊断，不是另一套默认验收判据。

本次使用同一确定性N4096/Q4|P1真实SEAL fixture，复放原始未取整指令后，
导回SEAL并用主机重建的私钥解密；私钥不进入交付包。比较全部2048个复数槽。

| 输入scale | 原始word失配数 | 未取整与SEAL最大解密差额 | 未取整与明文最大误差 | SEAL与明文最大误差 |
|---|---:|---:|---:|---:|
| 2^20 | 32768 | 6.16051e-8 | 3.82426348e-4 | 3.82377059e-4 |
| 2^30 | 32768 | 5.88216e-14 | 4.57457074e-7 | 4.57457020e-7 |

这个单步案例的解密差额小，但不等于raw word相同，也不能推广到全部参数和长应用链。
本版包的`oracle/report.json`明确记录`golden_source=SEAL ciphertext in HPU layout`；
因此“使用当前应用包golden”并不自动改为HPU未取整语义。
采用未取整验收需要独立生成该语义的reference/golden，并以解密误差与SEAL对照。
不能直接把被测指令复放的输出复制成golden，也不能把整数比较改成随意的容限。
`hpu_ckks_precision`提供可复现的主机精度测量入口，不用于目标执行判定。

## 同步、性能和诊断

窗口只配置实际使用容量，64-line guard放在窗口外的有效DDR。
输出先填非规范poison；golden单独嵌入，不预装输出。
完整程序所有DSTORE之后只有一次PSYNC，通过MMIO IRQ与BUSY共同同步，
再清电平、自检；本组不初始化PLIC或计时器中断。

发射后等待预算默认25000000个CPU cycle，每64轮读一次cycle。
可用`HPU_APPLICATION_TIMEOUT_CYCLES`构建参数调整。它不是整例cycle-limit；
custom指令阻塞发射时，CPU尚不能执行软件超时。

`application_trace`不打印，记录阶段、最近DMA的指令编号/word、DMA编号及
wait status/irq/fault/cycle。阶段1..7依次为prepare/configure/issue/wait/compare/guard/done。
读取需CPU缓存感知调试，不应假设脏cache中的记录已写回原始DDR。
算术发射停住时，记录定位到前一个DMA的区间，再结合PC和随包反汇编。

准备阶段采用64-bit、64-byte分块复制；guard仅失效实际读取的只读区。
完整输入、逐算子结果、padding和guard判据保留。CPU cycle改善需要IT实测。
默认普通版只输出开始/结束与失败首错；`_silent`关闭UART，full dump仍为显式选项。
N65536 x²+1保留部署规模；CPU复制/cache维护与比较本身需要大量周期。
AM的1GiB链接内存并不能代替IT内存容量确认，必须覆盖ELF、栈及完整HPU DDR窗口。
