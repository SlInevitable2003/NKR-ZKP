# 公共算子层

本目录包含 Groth16 和 PLONK 共用、与具体协议无关的计算原语。协议方程、证明类型、Transcript、QAP 逻辑，以及 PLONK 的置换与商多项式逻辑都不应放在这里。

- `cuda/`：BN254 域、曲线、NTT、MSM 及设备运行时辅助功能的 NVIDIA 实现。
- `hip/`：同一组算子的 HIP 实现。
- `include/nkr/operators/`：CPU 协议共用的调用层算子。目前提供基于 libff
  窗口表的固定基批量标量乘，以及显式分块的并行变基 MSM。

CUDA 和 HIP 有意保留各自独立的内核。两者实现相同的抽象算子边界，并使用与协议无关的测试向量进行验证，但不会通过宏强行合并为同一份内核源码。

CPU 算子不会开启 libff/libfqfft 的全局 `MULTICORE` 宏。Setup 在公共算子的
调用层使用 OpenMP；Prove 仍调用单线程 libff 原语，避免改变 CPU 基准口径。
