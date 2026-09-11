# 03 指令测试独立子项

这里单独维护可并行的子项清单，不替换原九个完整用例，也不复制 37 份 C 源码。每个子项引用原 `src/03_compute_instructions/` 中的源文件，在构建时固化 `subcase=N`，生成一个独立 ELF/BIN。仿真时直接装载对应文件，无须再给仿真器传子项选择参数。

新的下载包名称为 `nexus-am-hpu-subtests`。本地交付目录为 `build/subtests/release/03_compute_instructions/`，每个 `subtest_id` 对应同名 `.elf`、`.bin`、`.txt`（反汇编）。原常规包和全量 UART 诊断包保持不变；子项包默认使用常规摘要输出，仍保留阶段日志、错误项和自检。

## 构建与下载

首次先准备并验证固定producer数据；已经运行过`make all`时不用再生成一遍：

```bash
export AM_HOME=/path/to/nexus-am
make -C tests/hputest verify-inline-asm
make -C tests/hputest subtests JOBS=4
```

`JOBS`是编译并发数，不是仿真并发数。子项链接保持串行，因为AM库共享`mainargs.S`；
每次以`mainargs=subcase=N`重新链接，并从实际ELF加载段读取参数验证，不能把同一个全轮ELF复制改名凑数。
原C源码不变，选择参数已在每个ELF/BIN中固化，仿真运行时无需另传`subcase=N`。

GitHub Actions追加发布`nexus-am-hpu-subtests`，不替换原有两个包。新包包含：

- `03_compute_instructions/`：37组独立ELF/BIN/反汇编；
- `INDEX.tsv`：父用例、选择号、说明、真实文件名；`cases.tsv`是对应源清单；
- `MANIFEST.txt`及`provenance/`：AM/inline版本、数据与编码来源；
- `tools/run-subtests.py`：用例外并行/可选绑核；`tools/parse-uart-results.py`：UART结果提取。

每项包含自己的输入数据；无需在服务器上重新编译或手改ELF。

## 拆分范围

| 原用例 | 指令 | 独立 ELF 数量 | 每个 ELF 保留的内容 |
| --- | --- | ---: | --- |
| 001 | PADD | 4 | 基础独立目的、边界覆盖 p0、边界覆盖 p1、连续依赖各一项 |
| 002 | PSUB | 4 | 基础独立目的、边界覆盖 p0、边界覆盖 p1、连续依赖各一项 |
| 003 | PMUL | 6 | 基础对象、基础立即数 7、边界覆盖 p0、边界立即数 0/1/255 |
| 004 | PMAC | 4 | 初值 0、初值 q−1 连续两次乘加、初值 1/立即数 255、目的覆盖 p0 |
| 005 | PNTT | 6 | 基础/边界数据分别执行单 stage 0、1、11 |
| 006 | PINTT | 6 | 基础/边界数据分别执行单逆 stage 0、1、11 |
| 007 | PMODLD | 2 | 基础/边界两项，每项保留 q0→q1→q0 和三组输出 |
| 008 | PSYNC | 3 | 空闲/DMA/计算三项，每项保留连续两轮中断 |
| 009 | PFREE | 2 | 基础 p0/边界 p7 两项，每项保留释放后复用 |

合计 **37 个独立子项、40 段完整程序**。`programs` 统计末尾各有一次 PSYNC 的完整程序数量，不是 HPU 指令条数；008 每项含两段程序，其余每项一段。完整覆盖一个父用例，需要其全部子项分别通过；仅一个子项 PASS 不能记为父用例全部通过。

## 不能拆开的依赖

- PADD/PSUB 的连续依赖和 PMAC 的连续两次乘加，仍在同一个 ELF 内保留原序列。
- PMODLD 的三个上下文选择、驻留输入、对象释放复用及三组输出检查保持在同一段程序内，不能拆成三个无关联任务。
- PSYNC 的 `irq_open()` 第一轮和 `irq_rearm()` 第二轮保持在同一个 ELF，才能继续验证第二次中断和清除后的重新使能。它的子项号是 `scenario=0/1/2`，不是 UART `result_context` 使用的 `scenario*2+round`。
- 005/006 原本就是互相独立的单 stage 测试；它们不是整体 NTT/INTT 的 stage 链。本目录不拆分 04 章的完整变换链。

每个 ELF 都自行准备输入、软件 golden 和 guard，配置自己的仿真实例中的 HPU 窗口，发指令、等待完成并自检。不依赖另一个 ELF 留下的对象、DDR 数据、中断状态或模上下文，也不跳过原子项内部的检查来缩短时间。

## 如何并行运行

并行单位是**相互独立的仿真进程**：一个进程装载一个子项 ELF，使用独立工作目录、UART 日志、波形文件和仿真临时文件。主机允许时，可在外层调度器用 `taskset` 等方式把不同进程绑定到不同 CPU 核；这不是让一个 ELF 在被仿真的多核 CPU 中同时执行，也不修改 VCS FGP 配置。

例如 001 的四项可以作为四个独立任务同时排队，任务名为：

```text
HPU_IT_DIR_INS_C0_001__s00_basic_dst_p2
HPU_IT_DIR_INS_C0_001__s01_boundary_alias_p0
HPU_IT_DIR_INS_C0_001__s02_boundary_alias_p1
HPU_IT_DIR_INS_C0_001__s03_boundary_dependent
```

具体 `simv` 装载参数和 PASS 判定沿用已验证的 IT 脚本，本目录不假定其命令行接口。并发数受服务器内存、CPU 配额和仿真许可证等限制；每个任务仍有启动、初始化、golden 和 guard 成本，不能保证四任务等于四倍加速。退出码、最终 PASS/FAIL 和 UART 阶段日志都应分别保存；任一任务超时或失败都不能当作完成覆盖。

### 只并行跑01的四项

包内调度器可按名称前缀选择。下面是命令模板，`<真实IT参数>`需要替换，不能原样执行：
把你们原启动命令里的ELF路径替换为`{elf}`，其它仿真参数保持原样；程序/脚本须使用绝对路径并在前台等待仿真结束。

```text
python3 tools/run-subtests.py --package /data/subtests \
  --id-prefix HPU_IT_DIR_INS_C0_001__ \
  --jobs 4 --run-dir /data/runs/padd-first \
  --cpus 0,1,2,3 \
  -- /absolute/path/to/simv <真实IT参数及含{elf}的加载参数>
```

`--cpus`可省略；指定时每个并发槽绑定一个当前进程允许使用的host CPU，编号需按服务器实际配额填写。
不要把这里的CPU编号当作DUT核号。`--run-dir`必须是新目录，每项有独立工作目录和`sim.log`，避免波形/临时文件互相覆盖。
若原IT命令依赖相对路径配置文件，也需改成绝对路径或使用负责准备环境的前台wrapper。
可选`--timeout-seconds`是墙钟时间，不是VCS cycle-limit；工具不会猜测或修改仿真周期预算。

汇总`summary.tsv`只报告进程退出码、超时、CPU与耗时，`verdict=NOT_EVALUATED`。
工具退出0不代表所有用例PASS，最终仍需按IT日志和用例自检判定。

## 清单格式

`cases.tsv` 使用制表符分隔，列固定为：

| 列 | 含义 |
| --- | --- |
| `parent_case_id` | 原用例 ID，对应原来的一个 C 文件 |
| `subcase` | 从 0 开始的选择号，与源码 `subcase_selected()` 和 `mainargs` 一致 |
| `subtest_id` | 唯一安全 ASCII 名称，含父用例 ID、选择号和语义后缀 |
| `programs` | 本子项保留的完整程序数量 |
| `description` | 中文测试内容，公式均按用例模数求模 |
| `source` | 相对 `tests/hputest/` 的原 C 源码路径 |

005/006 使用 `subcase=profile*3+selected`：0/1/2 是基础数据 stage 0/1/11，3/4/5 是边界数据 stage 0/1/11。PINTT 的 stage 号是逆向指令参数，不要换成其内部对应的正向 loader stage 号。
