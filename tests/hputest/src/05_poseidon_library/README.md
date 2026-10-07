# 05：算法库层

`01_ckks`、`02_bfv`、`03_bgv`：30个Poseidon接口闭环用例，三方案、两规模、五类操作。

`04_parameter_regression`：7个SEAL参考参数回归，保留BGV非平凡correction factor
与generator-3步长1旋转。它们与Poseidon基础例的参数不同，不宣称调用Poseidon接口。
文件名直接标出CF或旋转约定，避免只靠编号判断是否重复。

`05_pending_interfaces`：Encode和Bootstrap的真实缺口记录，不发布ELF、不返回假PASS。

05验收完整库操作与密文分量/level/scale/correction factor；04验收基本算子和对象链。
用例仍一项一个C文件，每个ELF执行一遍，DSTORE后唯一PSYNC，默认摘要并提供silent版。
具体参考机制、BFV差异与旋转限制见[接口说明](../../docs/POSEIDON_LIBRARY.md)。
