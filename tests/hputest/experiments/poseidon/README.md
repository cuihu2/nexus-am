# Poseidon独立实验（不是默认验证）

当前05组以inline-asm内置modified-SEAL为参考。此目录仅保留此前的Poseidon对照实验：
30个CKKS/BFV/BGV×N128/N4096接口用例、主机桥接工具、接收器及固定SDK。
不在默认清单和下载包中，默认构建无需Poseidon/GMP。

Poseidon SDK固定main `961df3acfc394b634de3fd846946c851903a0e72`。
源代码在`third_party/poseidon`；原参考库曾出现BFV乘法噪声密文与modified-SEAL不同，
N4096三分量tensor和最终结果合计81920个原始word不同，但8192个解密明文系数相同。
该明文语义对拍只属于此实验，不得冒充当前SEAL逐word验收。

Poseidon的旋转generator为5，SEAL为3。实验保留共同Galois元素N+1的半行旋转；
不宣称任意旋转步长已覆盖。目标AM执行生成指令，不直接调用Poseidon硬件驱动。

## 显式入口

先手动初始化本实验submodule（默认Actions不会下载它），并按照Poseidon自己的说明
单独构建软件库，关闭硬件后端和可选压缩依赖。仅本实验需要GMP。
再配置`tools/CMakeLists.txt`，传入：

- `INLINE_ASM_ROOT`：固定的`third_party/hpu-applications`绝对路径；
- `POSEIDON_ROOT`：此目录`third_party/poseidon`绝对路径；
- `POSEIDON_BUILD`：已经显式构建的Poseidon软件库目录。

构建目标为`hpu_poseidon_case_generator`。
`scheme-cases.tsv`是本实验的独立清单；不把它追加到默认清单。
生成器调用真实Poseidon API并写包外`*.poseidon.json`，
显式接收器为`scripts/import-application-package.py`，
验证脚本为`scripts/test-poseidon-delivery.py`。
接收器必须同时给定inline-asm和Poseidon提交，不可省略来源门禁。

`src/01_ckks`、`02_bfv`、`03_bgv`保存原AM源码。
此目录的构建和对拍结果不得计入默认05或宣称IT/VCS已通过。
