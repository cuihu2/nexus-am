# Third-party 依赖说明

## inline-asm

- 项目地址：<https://github.com/cuihu2/inline-asm>
- 接入方式：Git submodule
- 本地路径：`third_party/inline-asm`
- 固定提交：`4399883b9e1fa249b99d48c7e919ee52acc662bc`
- 用途：HPU 指令编码、算子流生成、line map、软件 reference 和 golden 数据

本仓库通过 submodule 记录上游地址和固定提交。该提交中未发现根许可证文件；
使用、修改或再分发前应由项目所有者确认授权范围。

根目录 `hpu_inline_asm.h` 是本项目的 GNU RISC-V 指令适配层，不是上游源码
副本；其中固定指令字必须与上述提交的 encoder/self-test 保持一致。

## OpenXiangShan/nexus-am

- 项目地址：<https://github.com/OpenXiangShan/nexus-am>
- 接入方式：构建时通过 `AM_HOME` 指向外部 checkout
- 验证提交：`f308903f8c623e82f8775ebf04ffffc74d330dc0`
- 用途：`riscv64-xs` 启动代码、链接脚本、Abstract Machine 和 klib

本仓库不复制 nexus-am 源码，只保存构建适配 `nexus-am.mk`。验证提交中未发现
仓库根许可证文件；其源码和子目录可能具有各自授权，分发 ELF/库前应完成
依赖许可证审查。

## IT-SCPU-RTL

- 用户给定地址：<https://github.com/mmmsssttt404/IT-SCPU-RTL>
- 本地验证提交：`1664eb058e0499dd13814ef77108d80f12e9ef7d`
- 用途：HPU Spike reference、Difftest runner 和最终 RTL 执行环境

IT-SCPU-RTL 是独立交付/执行环境，不作为本仓库的 submodule 或再分发内容。
