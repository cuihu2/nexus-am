# HPU 内联汇编指令与机器码转译手册

本文件是指令速查说明。完整编程模型、32-bit 位域、逐指令操作和推荐程序序列见 [`HPU_PROGRAMMING_MANUAL.md`](HPU_PROGRAMMING_MANUAL.md)。

本手册描述当前项目采用的 HPU 软件接口。体系结构只保留 11 条指令：9 条 `custom0` 内部执行指令和 2 条 `custom1` DMA 指令。历史版本中的 `pshcfg`、`pshuf`、`pseed`、`psample` 已删除，编码器必须拒绝这些助记符。

> 26-bit 命令以最高位 `cmd26[25]` 表示 `cmd_kind`。custom0 使用 `{1'b0, inst[31:7]}`；custom1 将原始指令中的 `flag/OBJ_ID/TYPE/DIR` 重排到文档规定的 payload，`rs1/rs2` 转为独立 sideband。原始 custom1 的 `inst[8]` 承载 `dload flag[0]` small-bank hint。

## 1. 指令总表

| 类别 | 指令 | 格式 | custom0 OPC | 语义 |
| --- | --- | --- | --- | --- |
| 算术 | `padd` | AR3 | `0000` | 模加 |
| 算术 | `psub` | AR3 | `0001` | 模减 |
| 算术 | `pmul` | AR3 | `0010` | 模乘 |
| 算术 | `pmac` | AR3 | `0011` | 模乘加 |
| 变换 | `pntt` | STG | `0100` | NTT stage，同一逻辑对象号 |
| 变换 | `pintt` | STG | `0101` | INTT stage，同一逻辑对象号 |
| 上下文 | `pmodld` | MOD | `0110` | 按 8-bit `MOD_ID` 激活固定模表项 |
| 通知 | `psync` | SYNC | `0111` | 程序末尾通知 CPU 执行完成 |
| 生命周期 | `pfree` | CFG | `1000` | 释放对象槽位地址空间 |
| 外部访存 | `dload` | DMA | - | 外存加载到对象槽位 |
| 外部访存 | `dstore` | DMA | - | 对象槽位写回外存 |

`custom0` 的固定低 7-bit opcode 为 `0001011`，`inst[31:28]` 为表中的 OPC。`1001` 到 `1111` 保留，不得分配给旧指令。

## 2. AR3 算术指令

对象模式语法：

```asm
padd p2, p0, p1
psub p2, p0, p1
pmul p2, p0, p1
pmac p2, p0, p1
```

只有 `pmul` 和 `pmac` 支持 8-bit 小立即数，仍使用原助记符，由第三操作数类型选择 `MODE[0]=1`：

```asm
pmul p2, p0, 255
pmac p2, p0, 255
```

`paddi`、`psubi`、`pmuli`、`pmaci` 不是体系结构指令。当前解析器也不接受 `padd/psub` 的立即数形式。对象槽位范围为 `p0` 到 `p7`，`cimm8` 范围为 0 到 255。

## 3. STG 变换指令

```asm
pntt pdata, ptwiddle, stage, mode, flag
pintt pdata, ptwiddle, stage, mode, flag
```

`pdata` 是逻辑读写对象，`ptwiddle` 是当前 stage 的 twiddle 对象。`stage` 为 4-bit，`mode` 为 2-bit，`flag` 为 1-bit。生成器在每个 stage 执行以下生命周期：

```asm
dload ..., ptwiddle, 1, 0
pntt pdata, ptwiddle, stage, mode, flag
pfree ptwiddle
```

INTT 同理。`pfree` 只释放当前 stage 的 twiddle；`pdata` 仍保留给下一 stage 或后续 `dstore`。

## 4. CFG 指令

### `pmodld`

```asm
pmodld mod_id
```

从 small Bank 5 的模上下文表中选择并激活表项。`mod_id` 为 8-bit，编码在原始指令 `[21:14]`，经过 custom0 precode 后位于 `cmd26[14:7]`。Bank 5 为 32 line、固定基址 `0x1400`，但 8-bit `MOD_ID` 最多寻址 256 个 context。

```text
line = MOD_TABLE_BASE_LINE + (mod_id >> 4)
slot = mod_id & 0xF
```

模表对象由 `dload type=2, flag[0]=1` 分配到 Bank 5。DMA 与后续 `pmodld` 的一致性由硬件维护，软件无需在两者之间插入 `psync`。该对象仍具有 `ALLOC/V/busy/base/len` 状态，但对象号不编码进 `pmodld`。旧语法 `pmodld psrc, idx1, cfg` 不再接受。

### `pfree`

```asm
pfree psrc
```

`PSRC/OBJ_ID` 指定要释放的对象槽位，其余字段必须为 0。`pfree` 按队列顺序执行，应放在该对象最后一次读取之后。示例 `pfree p4` 的编码为 `0x8100000B`。

## 5. 程序完成通知指令

```asm
psync
```

`psync` 没有操作数，所有载荷位为 0。软件只在一段完整 HPU 程序的最后发出一次，用于通知 CPU 整个程序已经完成。DMA 与计算之间的顺序、一致性和对象可见性由硬件维护，因此 `psync` 不用于等待 DMA，也不作为算子内部的阶段屏障；它同样不能代替对象生命周期管理。

## 6. custom1 DMA 指令

```asm
dload  rs1, rs2, pdst, load_type, small_bank
dstore rs1, rs2, psrc, rel
```

| 指令 | 字段 | 约定 |
| --- | --- | --- |
| `dload` | `load_type` | `0=seg`、`1=poly`、`2=mod_ctx`；编码值 `3` 保留且软件拒绝 |
| `dload` | `small_bank` | `0=普通 bank`、`1=设置 flag[0] 并请求 small Bank 5` |
| `dstore` | `rel` | `0=写回后保留`、`1=写回完成后释放` |

`custom1` 固定低 7-bit opcode 为 `0101011`。`small_bank` 编码在原始 `inst[8]`，`inst[7]` 保留为 0；precode 将它映射到 `cmd26[10]`，即控制逻辑 `flag[0]`。`rs1/rs2` 形成 line offset/count sideband，不进入 26-bit 命令本体。模表加载必须使用 `dload ..., 2, 1`。

## 7. 对象生命周期规则

1. `dload` 成功后，目标对象槽位进入 live 状态；再次写同一槽位前必须确保旧对象已释放。
2. 算术和变换指令读取源对象；`pmodld` 只读取固定模表中的 `MOD_ID` 表项。
3. 不需要写回的输入、常量、twiddle 和模上下文，在最后一次使用后执行 `pfree`。
4. 需要写回且之后不再使用的结果执行 `dstore ..., 1`，由 DMA 完成后释放，不能再对同一对象追加 `pfree`。
5. 仍要复用的对象执行 `dstore ..., 0` 或暂不释放，直到真实的最后一次使用。

完整算子流已按这些规则生成 `pfree`。RV 冒烟用例同时覆盖 `pfree` 正向编码，以及旧指令和非法 `pfree` 操作数的拒绝行为。
