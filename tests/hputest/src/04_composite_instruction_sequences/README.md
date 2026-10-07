# 04：基本/组合算子层

本组只验证BConv、整体NTT/INTT、KeySwitch、Auto和指令链编排。
当前11项：10项有自检，1项指令链异常场景尚未接入，不发布占位ELF。

KeySwitch的c0/c1置零，隔离c2切换与ModUp/NTT/密钥乘加/INTT/ModDown；
不把完整密文的HMUL、Reline、Rescale/ModSwitch、Rotate放在本组。
旧Auto项验证Q4/P3/D2的NTT、专用twiddle和Galois KeySwitch链，不作为算法库Rotate接口验收。
这些算法库接口属于05，即使内部会调用本组的基本算子，也不重复计为04接口测试。

旧04算法库条目的合并和迁移见[迁移清单](../../layer-migration.tsv)与
[05说明](../../docs/SEAL_LIBRARY.md)。03仍测试单条HPU指令，07仍测试完整应用。
